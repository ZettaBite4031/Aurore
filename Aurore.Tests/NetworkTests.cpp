#include <gtest/gtest.h>

#include <Aurore/Network/NetworkManager.hpp>
#include <Aurore/Network/NetworkTypes.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <../src/NetworkBackend.hpp>
#include <../src/NetworkManagerTestAccess.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
	using namespace Aurore::Network;
	using namespace Aurore::Network::Detail;
	using Aurore::Util::ByteBuffer;

	constexpr ConnectionId FirstTestConnection{ .Value = 1 };
	constexpr ConnectionId SecondTestConnection{ .Value = 2 };

	enum class FakeBackendCall : std::uint8_t {
		Initialize,
		Start,
		NotifyCommandAvailable,
		Stop,
		Shutdown,
	};

	struct FakeBackendState final {
		std::size_t InitializeCalls{ 0 };
		std::size_t StartCalls{ 0 };
		std::size_t NotifyCalls{ 0 };
		std::size_t StopCalls{ 0 };
		std::size_t ShutdownCalls{ 0 };

		bool Initialized{ false };
		bool Running{ false };

		std::optional<NetworkError> InitializeFailure;
		std::deque<NetworkResult<NetworkEndpoint>> StartResults;
		NetworkEndpoint Endpoint{ .Address = "127.0.0.1", .Port = 25565 };

		NetworkCommandQueue* Commands{ nullptr };
		NetworkEventQueue* Events{ nullptr };
		NetworkResourceLedger* Resources{ nullptr };

		std::vector<ConnectionId> ConnectionsToRegister;
		std::vector<ConnectionId> RegisteredConnections;
		std::optional<std::vector<std::byte>> InboundEventOnStart;
		std::vector<FakeBackendCall> Calls;
	};

	class FakeNetworkBackend final : public NetworkBackend {
	public:
		explicit FakeNetworkBackend(std::shared_ptr<FakeBackendState> state) : m_State(std::move(state)) {}

		NetworkResult<void> Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) override {
			(void)config;
			m_State->InitializeCalls++;
			m_State->Calls.push_back(FakeBackendCall::Initialize);
			if (m_State->InitializeFailure.has_value()) return std::unexpected(*m_State->InitializeFailure);

			m_State->Commands = &commands;
			m_State->Events = &events;
			m_State->Resources = &resources;
			m_State->Initialized = true;
			m_State->Running = false;
			return {};
		}

		NetworkResult<NetworkEndpoint> Start() override {
			m_State->StartCalls++;
			m_State->Calls.push_back(FakeBackendCall::Start);
			if (!m_State->Initialized) return std::unexpected(NetworkError::NotInitialized);
			if (m_State->Running) return std::unexpected(NetworkError::AlreadyRunning);

			NetworkResult<NetworkEndpoint> result{ m_State->Endpoint };
			if (!m_State->StartResults.empty()) {
				result = std::move(m_State->StartResults.front());
				m_State->StartResults.pop_front();
			}
			if (!result.has_value()) return result;

			for (const ConnectionId connection : m_State->ConnectionsToRegister) {
				if (m_State->Resources == nullptr || !m_State->Resources->RegisterConnection(connection)) {
					DeactivateConnections();
					return std::unexpected(NetworkError::BackendFailure);
				}
				m_State->RegisteredConnections.push_back(connection);
			}

			if (m_State->InboundEventOnStart.has_value()) {
				if (m_State->Resources == nullptr || m_State->Events == nullptr || m_State->RegisteredConnections.empty()) {
					DeactivateConnections();
					return std::unexpected(NetworkError::BackendFailure);
				}

				auto reservation = m_State->Resources->ReserveInboundEvent(m_State->RegisteredConnections.front(), m_State->InboundEventOnStart->size());
				if (!reservation.has_value()) {
					DeactivateConnections();
					return std::unexpected(reservation.error() == NetworkResourceLedger::ReserveError::InvalidConnection ? NetworkError::InvalidConnectionId : NetworkError::InboundLimitExceeded);
				}

				const auto push_result = m_State->Events->Push(QueuedNetworkEvent{
					.Event = BytesReceivedEvent{.Connection = m_State->RegisteredConnections.front(), .Data = std::move(*m_State->InboundEventOnStart) },
					.InboundReservation = std::move(*reservation),
					});
				m_State->InboundEventOnStart.reset();
				if (push_result != QueuePushResult::Queued) {
					DeactivateConnections();
					return std::unexpected(NetworkError::EventQueueLimitExceeded);
				}
			}

			m_State->Running = true;
			return result;
		}

		void NotifyCommandAvailable() noexcept override {
			m_State->NotifyCalls++;
			m_State->Calls.push_back(FakeBackendCall::NotifyCommandAvailable);
		}

		void Stop() noexcept override {
			m_State->StopCalls++;
			m_State->Calls.push_back(FakeBackendCall::Stop);
			DeactivateConnections();
			m_State->Running = false;
		}

		void Shutdown() noexcept override {
			m_State->ShutdownCalls++;
			m_State->Calls.push_back(FakeBackendCall::Shutdown);
			DeactivateConnections();
			m_State->Running = false;
			m_State->Initialized = false;
			m_State->Commands = nullptr;
			m_State->Events = nullptr;
			m_State->Resources = nullptr;
		}

	private:
		void DeactivateConnections() noexcept {
			if (m_State->Resources != nullptr) {
				for (const ConnectionId connection : m_State->RegisteredConnections) m_State->Resources->DeactivateConnection(connection);
			}
			m_State->RegisteredConnections.clear();
		}

		std::shared_ptr<FakeBackendState> m_State;
	};

	NetworkConfiguration MakeTestConfig() {
		return NetworkConfiguration{
			.Backend = NetworkBackendType::Automatic,
			.BindAddress = "127.0.0.1",
			.Port = 0,
			.MaximumConnections = 16,
			.ReceiveBufferSize = 4096,
			.MaximumInboundBytesPerConnection = 64 * 1024,
			.MaximumOutboundBytesPerConnection = 64 * 1024,
			.MaximumCommandQueueEntries = 256,
			.MaximumCommandQueueBytes = 1024 * 1024,
			.MaximumEventQueueEntries = 256,
			.MaximumEventQueueBytes = 1024 * 1024,
			.MaximumTotalOutboundBytes = 4 * 1024 * 1024,
			.MaximumTotalInboundEventBytes = 4 * 1024 * 1024,
		};
	}

	ByteBuffer MakeBuffer(std::size_t size, std::byte value = std::byte{ 0x5A }) {
		std::vector<std::byte> bytes(size, value);
		ByteBuffer result;
		result.WriteBytes(std::span<const std::byte>(bytes.data(), bytes.size()));
		return result;
	}

	std::vector<NetworkEvent> DrainEvents(NetworkEventQueue& queue) {
		auto queued = queue.Drain();
		std::vector<NetworkEvent> result;
		result.reserve(queued.size());
		for (auto& value : queued) result.push_back(std::move(value.Event));
		return result;
	}

	void ExpectNoResources(const NetworkResourceSnapshot& snapshot) {
		EXPECT_EQ(snapshot.TrackedConnections, 0);
		EXPECT_EQ(snapshot.ActiveConnections, 0);
		EXPECT_EQ(snapshot.TotalOutboundBytes, 0);
		EXPECT_EQ(snapshot.TotalInboundEventBytes, 0);
	}

	TEST(NetworkQueueTests, EnforcesEntryLimit) {
		NetworkCommandQueue queue;
		queue.Reset(QueueLimits{ .MaximumEntries = 1, .MaximumBytes = 64 });
		EXPECT_EQ(queue.Push(StopCommand{}), QueuePushResult::Queued);
		EXPECT_EQ(queue.Push(ResumeReceiveCommand{ .Connection = FirstTestConnection }), QueuePushResult::EntryLimitExceeded);
		const auto snapshot = queue.GetSnapshot();
		EXPECT_EQ(snapshot.Entries, 1);
		EXPECT_EQ(snapshot.Bytes, 0);
	}

	TEST(NetworkQueueTests, EnforcesByteLimitAndReleasesWeightWhenPopped) {
		NetworkCommandQueue queue;
		queue.Reset(QueueLimits{ .MaximumEntries = 4, .MaximumBytes = 4 });
		EXPECT_EQ(queue.Push(SendCommand{ .Connection = FirstTestConnection, .Data = MakeBuffer(4) }), QueuePushResult::Queued);
		EXPECT_EQ(queue.GetSnapshot().Bytes, 4);
		EXPECT_EQ(queue.Push(SendCommand{ .Connection = FirstTestConnection, .Data = MakeBuffer(1) }), QueuePushResult::ByteLimitExceeded);
		ASSERT_TRUE(queue.TryPop().has_value());
		EXPECT_EQ(queue.GetSnapshot().Bytes, 0);
		EXPECT_TRUE(queue.Empty());
	}

	TEST(NetworkResourceLedgerTests, EnforcesPerConnectionAndGlobalOutboundLimits) {
		NetworkResourceLedger ledger;
		ASSERT_TRUE(ledger.Reset(NetworkResourceLedger::Limits{
			.MaximumOutboundBytesPerConnection = 8,
			.MaximumInboundEventBytesPerConnection = 8,
			.MaximumTotalOutboundBytes = 12,
			.MaximumTotalInboundEventBytes = 16,
			}));
		ASSERT_TRUE(ledger.RegisterConnection(FirstTestConnection));
		ASSERT_TRUE(ledger.RegisterConnection(SecondTestConnection));

		auto first = ledger.ReserveOutbound(FirstTestConnection, 8);
		ASSERT_TRUE(first.has_value());
		auto per_connection_failure = ledger.ReserveOutbound(FirstTestConnection, 1);
		ASSERT_FALSE(per_connection_failure.has_value());
		EXPECT_EQ(per_connection_failure.error(), NetworkResourceLedger::ReserveError::PerConnectionLimitExceeded);

		auto second = ledger.ReserveOutbound(SecondTestConnection, 4);
		ASSERT_TRUE(second.has_value());
		auto global_failure = ledger.ReserveOutbound(SecondTestConnection, 1);
		ASSERT_FALSE(global_failure.has_value());
		EXPECT_EQ(global_failure.error(), NetworkResourceLedger::ReserveError::GlobalLimitExceeded);

		first->Reset();
		second->Reset();
		ledger.DeactivateConnection(FirstTestConnection);
		ledger.DeactivateConnection(SecondTestConnection);
		ExpectNoResources(ledger.GetSnapshot());
	}

	TEST(NetworkResourceLedgerTests, KeepsInactiveConnectionUntilReservationReleases) {
		NetworkResourceLedger ledger;
		ASSERT_TRUE(ledger.Reset(NetworkResourceLedger::Limits{
			.MaximumOutboundBytesPerConnection = 16,
			.MaximumInboundEventBytesPerConnection = 16,
			.MaximumTotalOutboundBytes = 16,
			.MaximumTotalInboundEventBytes = 16,
			}));
		ASSERT_TRUE(ledger.RegisterConnection(FirstTestConnection));
		auto reservation = ledger.ReserveOutbound(FirstTestConnection, 8);
		ASSERT_TRUE(reservation.has_value());

		ledger.DeactivateConnection(FirstTestConnection);
		auto snapshot = ledger.GetSnapshot();
		EXPECT_EQ(snapshot.TrackedConnections, 1);
		EXPECT_EQ(snapshot.ActiveConnections, 0);
		EXPECT_EQ(snapshot.TotalOutboundBytes, 8);

		reservation->Release(3);
		EXPECT_EQ(ledger.GetSnapshot().TotalOutboundBytes, 5);
		reservation->Reset();
		ExpectNoResources(ledger.GetSnapshot());
	}

	TEST(NetworkResourceLedgerTests, RejectedEventQueueAdmissionRollsBackInboundReservation) {
		NetworkResourceLedger ledger;
		NetworkEventQueue queue;
		ASSERT_TRUE(ledger.Reset(NetworkResourceLedger::Limits{
			.MaximumOutboundBytesPerConnection = 16,
			.MaximumInboundEventBytesPerConnection = 16,
			.MaximumTotalOutboundBytes = 16,
			.MaximumTotalInboundEventBytes = 16,
			}));
		ASSERT_TRUE(ledger.RegisterConnection(FirstTestConnection));
		queue.Reset(QueueLimits{ .MaximumEntries = 1, .MaximumBytes = 3 });

		auto reservation = ledger.ReserveInboundEvent(FirstTestConnection, 4);
		ASSERT_TRUE(reservation.has_value());
		EXPECT_EQ(queue.Push(QueuedNetworkEvent{
			.Event = BytesReceivedEvent{.Connection = FirstTestConnection, .Data = std::vector<std::byte>(4, std::byte{ 1 }) },
			.InboundReservation = std::move(*reservation),
			}), QueuePushResult::ByteLimitExceeded);
		EXPECT_EQ(ledger.GetSnapshot().TotalInboundEventBytes, 0);
		ledger.DeactivateConnection(FirstTestConnection);
		ExpectNoResources(ledger.GetSnapshot());
	}

	TEST(NetworkManagerConfigurationTests, RejectsEveryZeroOrInconsistentResourceBudget) {
		auto expect_invalid = [](NetworkConfiguration config) {
			NetworkManager manager;
			const auto result = manager.Initialize(std::move(config));
			ASSERT_FALSE(result.has_value());
			EXPECT_EQ(result.error(), NetworkError::InvalidConfiguration);
			};

		auto config = MakeTestConfig(); config.MaximumCommandQueueEntries = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumCommandQueueBytes = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumEventQueueEntries = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumEventQueueBytes = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumTotalOutboundBytes = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumTotalInboundEventBytes = 0; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumOutboundBytesPerConnection = config.MaximumTotalOutboundBytes + 1; expect_invalid(config);
		config = MakeTestConfig(); config.MaximumInboundBytesPerConnection = config.MaximumTotalInboundEventBytes + 1; expect_invalid(config);
		config = MakeTestConfig(); config.ReceiveBufferSize = config.MaximumEventQueueBytes + 1; expect_invalid(config);
	}

	TEST(NetworkManagerResourceTests, RejectsPerConnectionOutboundLimitBeforeBackendConsumption) {
		auto state = std::make_shared<FakeBackendState>();
		state->ConnectionsToRegister = { FirstTestConnection };
		auto config = MakeTestConfig();
		config.MaximumOutboundBytesPerConnection = 8;
		config.MaximumTotalOutboundBytes = 16;

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(config).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		ASSERT_TRUE(manager.QueueSend(FirstTestConnection, MakeBuffer(8)).has_value());

		const auto rejected = manager.QueueSend(FirstTestConnection, MakeBuffer(1));
		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error(), NetworkError::OutboundLimitExceeded);
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalOutboundBytes, 8);
		EXPECT_EQ(NetworkManagerTestAccess::GetCommandQueueSnapshot(manager).Bytes, 8);

		manager.Stop();
		ExpectNoResources(NetworkManagerTestAccess::GetResourceSnapshot(manager));
		manager.Shutdown();
	}

	TEST(NetworkManagerResourceTests, RejectsGlobalOutboundLimitAcrossConnections) {
		auto state = std::make_shared<FakeBackendState>();
		state->ConnectionsToRegister = { FirstTestConnection, SecondTestConnection };
		auto config = MakeTestConfig();
		config.MaximumOutboundBytesPerConnection = 8;
		config.MaximumTotalOutboundBytes = 8;

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(config).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		ASSERT_TRUE(manager.QueueSend(FirstTestConnection, MakeBuffer(8)).has_value());

		const auto rejected = manager.QueueSend(SecondTestConnection, MakeBuffer(1));
		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error(), NetworkError::OutboundLimitExceeded);
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalOutboundBytes, 8);
		manager.Stop();
		ExpectNoResources(NetworkManagerTestAccess::GetResourceSnapshot(manager));
		manager.Shutdown();
	}

	TEST(NetworkManagerResourceTests, CommandByteLimitFailureRollsBackOutboundReservation) {
		auto state = std::make_shared<FakeBackendState>();
		state->ConnectionsToRegister = { FirstTestConnection };
		auto config = MakeTestConfig();
		config.MaximumCommandQueueBytes = 4;

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(config).has_value());
		ASSERT_TRUE(manager.Start().has_value());

		const auto rejected = manager.QueueSend(FirstTestConnection, MakeBuffer(5));
		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error(), NetworkError::CommandQueueLimitExceeded);
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalOutboundBytes, 0);
		EXPECT_EQ(NetworkManagerTestAccess::GetCommandQueueSnapshot(manager).Entries, 0);
		manager.Shutdown();
	}

	TEST(NetworkManagerResourceTests, CommandEntryLimitFailureRollsBackOutboundReservation) {
		auto state = std::make_shared<FakeBackendState>();
		state->ConnectionsToRegister = { FirstTestConnection };
		auto config = MakeTestConfig();
		config.MaximumCommandQueueEntries = 1;

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(config).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		ASSERT_TRUE(manager.ResumeReceive(FirstTestConnection).has_value());

		const auto rejected = manager.QueueSend(FirstTestConnection, MakeBuffer(4));
		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error(), NetworkError::CommandQueueLimitExceeded);
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalOutboundBytes, 0);
		EXPECT_EQ(NetworkManagerTestAccess::GetCommandQueueSnapshot(manager).Entries, 1);
		manager.Shutdown();
	}

	TEST(NetworkManagerResourceTests, DrainEventsReleasesNetworkOwnedInboundReservation) {
		auto state = std::make_shared<FakeBackendState>();
		state->ConnectionsToRegister = { FirstTestConnection };
		state->InboundEventOnStart = std::vector<std::byte>(4, std::byte{ 0x2A });

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalInboundEventBytes, 4);
		EXPECT_EQ(NetworkManagerTestAccess::GetEventQueueSnapshot(manager).Bytes, 4);

		auto events = manager.DrainEvents();
		ASSERT_EQ(events.size(), 1);
		const auto* received = std::get_if<BytesReceivedEvent>(&events.front());
		ASSERT_NE(received, nullptr);
		EXPECT_EQ(received->Connection, FirstTestConnection);
		EXPECT_EQ(received->Data.size(), 4);
		EXPECT_EQ(NetworkManagerTestAccess::GetResourceSnapshot(manager).TotalInboundEventBytes, 0);
		EXPECT_EQ(NetworkManagerTestAccess::GetEventQueueSnapshot(manager).Entries, 0);
		manager.Shutdown();
	}

	TEST(NetworkManagerFakeBackendTests, FailedInitializationReturnsToUninitialized) {
		auto state = std::make_shared<FakeBackendState>();
		state->InitializeFailure = NetworkError::BackendFailure;

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		const auto result = manager.Initialize(MakeTestConfig());
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), NetworkError::BackendFailure);
		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.GetConfiguration().has_value());
		EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
		EXPECT_EQ(state->InitializeCalls, 1);
		EXPECT_EQ(state->StartCalls, 0);
		EXPECT_EQ(state->NotifyCalls, 0);
		EXPECT_EQ(state->StopCalls, 0);
		EXPECT_EQ(state->ShutdownCalls, 1);
		EXPECT_FALSE(state->Initialized);
		EXPECT_FALSE(state->Running);
		EXPECT_EQ(state->Commands, nullptr);
		EXPECT_EQ(state->Events, nullptr);
		EXPECT_EQ(state->Resources, nullptr);
		const std::vector expected_calls{ FakeBackendCall::Initialize, FakeBackendCall::Shutdown };
		EXPECT_EQ(state->Calls, expected_calls);
	}

	TEST(NetworkManagerFakeBackendTests, FailedStartReturnsToInitializedAndAllowsRetry) {
		auto state = std::make_shared<FakeBackendState>();
		state->Endpoint = NetworkEndpoint{ .Address = "127.0.0.1", .Port = 31000 };
		state->StartResults.emplace_back(std::unexpected(NetworkError::BackendFailure));
		state->StartResults.emplace_back(state->Endpoint);

		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());
		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_TRUE(state->Initialized);
		EXPECT_FALSE(state->Running);

		const auto first_start = manager.Start();
		ASSERT_FALSE(first_start.has_value());
		EXPECT_EQ(first_start.error(), NetworkError::BackendFailure);
		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
		EXPECT_TRUE(state->Initialized);
		EXPECT_FALSE(state->Running);

		const auto second_start = manager.Start();
		ASSERT_TRUE(second_start.has_value());
		EXPECT_EQ(*second_start, state->Endpoint);
		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_TRUE(manager.IsRunning());
		const auto bound = manager.GetBoundEndpoint();
		ASSERT_TRUE(bound.has_value());
		EXPECT_EQ(*bound, state->Endpoint);
		EXPECT_TRUE(state->Running);

		manager.Stop();
		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
		EXPECT_FALSE(state->Running);
		EXPECT_EQ(state->NotifyCalls, 1);
		EXPECT_EQ(state->StopCalls, 1);

		manager.Shutdown();
		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(state->Initialized);
		EXPECT_FALSE(state->Running);
		EXPECT_EQ(state->InitializeCalls, 1);
		EXPECT_EQ(state->StartCalls, 2);
		EXPECT_EQ(state->NotifyCalls, 1);
		EXPECT_EQ(state->StopCalls, 1);
		EXPECT_EQ(state->ShutdownCalls, 1);

		const std::vector expected_calls{
			FakeBackendCall::Initialize,
			FakeBackendCall::Start,
			FakeBackendCall::Start,
			FakeBackendCall::NotifyCommandAvailable,
			FakeBackendCall::Stop,
			FakeBackendCall::Shutdown,
		};
		EXPECT_EQ(state->Calls, expected_calls);
	}

	TEST(NetworkManagerFakeBackendTests, ShutdownWhileRunningStopsThenShutsDownBackend) {
		auto state = std::make_shared<FakeBackendState>();
		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		ASSERT_TRUE(manager.IsRunning());
		ASSERT_TRUE(state->Running);

		manager.Shutdown();
		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.GetConfiguration().has_value());
		EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
		EXPECT_FALSE(state->Initialized);
		EXPECT_FALSE(state->Running);
		EXPECT_EQ(state->InitializeCalls, 1);
		EXPECT_EQ(state->StartCalls, 1);
		EXPECT_EQ(state->NotifyCalls, 1);
		EXPECT_EQ(state->StopCalls, 1);
		EXPECT_EQ(state->ShutdownCalls, 1);

		const std::vector expected_calls{
			FakeBackendCall::Initialize,
			FakeBackendCall::Start,
			FakeBackendCall::NotifyCommandAvailable,
			FakeBackendCall::Stop,
			FakeBackendCall::Shutdown,
		};
		EXPECT_EQ(state->Calls, expected_calls);
	}

	TEST(NetworkManagerFakeBackendTests, DuplicateLifecycleRequestsDoNotReachBackend) {
		auto state = std::make_shared<FakeBackendState>();
		NetworkManager manager;
		ASSERT_TRUE(NetworkManagerTestAccess::InstallBackend(manager, std::make_unique<FakeNetworkBackend>(state)));
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());

		const auto duplicate_initialize = manager.Initialize(MakeTestConfig());
		ASSERT_FALSE(duplicate_initialize.has_value());
		EXPECT_EQ(duplicate_initialize.error(), NetworkError::AlreadyInitialized);
		EXPECT_EQ(state->InitializeCalls, 1);
		ASSERT_TRUE(manager.Start().has_value());

		const auto duplicate_start = manager.Start();
		ASSERT_FALSE(duplicate_start.has_value());
		EXPECT_EQ(duplicate_start.error(), NetworkError::AlreadyRunning);
		EXPECT_EQ(state->StartCalls, 1);

		manager.Shutdown();
		EXPECT_EQ(state->NotifyCalls, 1);
		EXPECT_EQ(state->StopCalls, 1);
		EXPECT_EQ(state->ShutdownCalls, 1);
	}
}
