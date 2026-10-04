#include <gtest/gtest.h>

#include <Aurore/Network/NetworkManager.hpp>
#include <Aurore/Network/NetworkTypes.hpp>

#include <../src/NetworkBackend.hpp>
#include <../src/NetworkManagerTestAccess.hpp>
#include <../src/windows/WindowsBackend.hpp>
#include <../src/windows/WindowsBackendTest.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
	using namespace Aurore::Network;
	using namespace Aurore::Network::Detail;
	using namespace Aurore::Network::Detail::Windows;

	NetworkConfiguration MakeTestConfig() {
		return NetworkConfiguration{
			.Backend = NetworkBackendType::Iocp,
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

	class WindowsWorkerTests : public ::testing::Test {
	protected:
		void SetUp() override {
			const auto config = MakeTestConfig();
			m_Commands.Reset(QueueLimits{ .MaximumEntries = config.MaximumCommandQueueEntries, .MaximumBytes = config.MaximumCommandQueueBytes });
			m_Events.Reset(QueueLimits{ .MaximumEntries = config.MaximumEventQueueEntries, .MaximumBytes = config.MaximumEventQueueBytes });
			ASSERT_TRUE(m_Resources.Reset(NetworkResourceLedger::Limits{
				.MaximumOutboundBytesPerConnection = config.MaximumOutboundBytesPerConnection,
				.MaximumInboundEventBytesPerConnection = config.MaximumInboundBytesPerConnection,
				.MaximumTotalOutboundBytes = config.MaximumTotalOutboundBytes,
				.MaximumTotalInboundEventBytes = config.MaximumTotalInboundEventBytes,
				}));
			ASSERT_TRUE(m_Backend.Initialize(config, m_Commands, m_Events, m_Resources).has_value());
		}

		void TearDown() override {
			m_Backend.Shutdown();
			m_Commands.Clear();
			m_Events.Clear();
			ExpectNoResources(m_Resources.GetSnapshot());
		}

		NetworkResourceLedger m_Resources;
		NetworkCommandQueue m_Commands;
		NetworkEventQueue m_Events;
		WindowsNetworkBackend m_Backend;
	};

	TEST_F(WindowsWorkerTests, NullControlCompletionDoesNotConsumeOperation) {
		auto* overlapped = WindowsNetworkBackendTestAccess::AddAcceptOperation(m_Backend);
		ASSERT_NE(overlapped, nullptr);
		ASSERT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 1);
		ASSERT_EQ(m_Commands.Push(StopCommand{}), QueuePushResult::Queued);

		const bool should_continue = WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, true, 0, CompletionKey::Command, nullptr, ERROR_SUCCESS);
		EXPECT_TRUE(should_continue);
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::IsShuttingDown(m_Backend));
		EXPECT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 1);
	}

	TEST_F(WindowsWorkerTests, SuccessfulSocketCompletionConsumesOperation) {
		WindowsNetworkBackendTestAccess::SetShuttingDown(m_Backend, true);
		auto* overlapped = WindowsNetworkBackendTestAccess::AddAcceptOperation(m_Backend);
		ASSERT_NE(overlapped, nullptr);
		ASSERT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 1);
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, true, 0, CompletionKey::Socket, overlapped, ERROR_SUCCESS));
		EXPECT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 0);
	}

	TEST_F(WindowsWorkerTests, CanceledSocketCompletionConsumesOperation) {
		WindowsNetworkBackendTestAccess::SetShuttingDown(m_Backend, true);
		auto* overlapped = WindowsNetworkBackendTestAccess::AddAcceptOperation(m_Backend);
		ASSERT_NE(overlapped, nullptr);
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, false, 0, CompletionKey::Socket, overlapped, ERROR_OPERATION_ABORTED));
		EXPECT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 0);
	}

	TEST_F(WindowsWorkerTests, UnknownOperationTriggersFatalShutdown) {
		OVERLAPPED unknown{};
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, true, 0, CompletionKey::Socket, &unknown, ERROR_SUCCESS));
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::IsShuttingDown(m_Backend));

		auto events = DrainEvents(m_Events);
		ASSERT_EQ(events.size(), 1);
		const auto* failure = std::get_if<NetworkFailureEvent>(&events.front());
		ASSERT_NE(failure, nullptr);
		EXPECT_EQ(failure->Error, NetworkError::BackendFailure);
		EXPECT_TRUE(failure->Fatal);
	}

	TEST_F(WindowsWorkerTests, ShutdownTimeoutExitsAfterOperationsDrain) {
		WindowsNetworkBackendTestAccess::SetShuttingDown(m_Backend, true);
		EXPECT_FALSE(WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, false, 0, CompletionKey::Socket, nullptr, WAIT_TIMEOUT));
	}

	TEST_F(WindowsWorkerTests, ShutdownTimeoutContinuesWhileOperationRemains) {
		WindowsNetworkBackendTestAccess::SetShuttingDown(m_Backend, true);
		ASSERT_NE(WindowsNetworkBackendTestAccess::AddAcceptOperation(m_Backend), nullptr);
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::ProcessCompletion(m_Backend, false, 0, CompletionKey::Socket, nullptr, WAIT_TIMEOUT));
	}

	TEST_F(WindowsWorkerTests, ExceptionContainmentPreservesOutstandingOperations) {
		ASSERT_NE(WindowsNetworkBackendTestAccess::AddAcceptOperation(m_Backend), nullptr);
		WindowsNetworkBackendTestAccess::HandleWorkerException(m_Backend, "Injected test failure");
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::IsStopRequested(m_Backend));
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::IsShuttingDown(m_Backend));
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::WasWorkerFailureReported(m_Backend));
		EXPECT_TRUE(WindowsNetworkBackendTestAccess::IsStartupComplete(m_Backend));
		EXPECT_EQ(WindowsNetworkBackendTestAccess::GetStartupError(m_Backend), NetworkError::BackendFailure);
		EXPECT_EQ(WindowsNetworkBackendTestAccess::OperationCount(m_Backend), 1);

		auto events = DrainEvents(m_Events);
		ASSERT_EQ(events.size(), 1);
		const auto* failure = std::get_if<NetworkFailureEvent>(&events.front());
		ASSERT_NE(failure, nullptr);
		EXPECT_TRUE(failure->Fatal);
		EXPECT_NE(failure->Message.find("Injected test failure"), std::string::npos);

		WindowsNetworkBackendTestAccess::HandleWorkerException(m_Backend, "Second failure");
		EXPECT_TRUE(DrainEvents(m_Events).empty());
	}

	TEST(IocpBackendLifecycleTests, SupportsRepeatedStartStopCycle) {
		NetworkManager manager;
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());
		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());

		for (int cycle{ 0 }; cycle < 15; cycle++) {
			SCOPED_TRACE(::testing::Message() << "Lifecycle cycle: " << cycle);
			const auto start = manager.Start();
			ASSERT_TRUE(start.has_value());
			EXPECT_NE(start->Port, 0);
			EXPECT_TRUE(manager.IsRunning());
			const auto bound = manager.GetBoundEndpoint();
			ASSERT_TRUE(bound.has_value());
			EXPECT_EQ(*bound, *start);
			manager.Stop();
			EXPECT_TRUE(manager.IsInitialized());
			EXPECT_FALSE(manager.IsRunning());
			EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
			ExpectNoResources(NetworkManagerTestAccess::GetResourceSnapshot(manager));
		}

		manager.Shutdown();
		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());
		ExpectNoResources(NetworkManagerTestAccess::GetResourceSnapshot(manager));
	}

	TEST(IocpBackendLifecycleTests, ShutdownStopsRunningBackend) {
		NetworkManager manager;
		ASSERT_TRUE(manager.Initialize(MakeTestConfig()).has_value());
		ASSERT_TRUE(manager.Start().has_value());
		ASSERT_TRUE(manager.IsRunning());
		manager.Shutdown();
		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.GetBoundEndpoint().has_value());
		ExpectNoResources(NetworkManagerTestAccess::GetResourceSnapshot(manager));
	}

}
