#include <Aurore/Network/NetworkManager.hpp>

#include "NetworkBackend.hpp"

#include <cassert>
#include <mutex>
#include <utility>

namespace {
	bool ValidateConfiguration(const Aurore::Network::NetworkConfiguration& config) noexcept {
		if (config.BindAddress.empty()) return false;
		if (config.MaximumConnections == 0) return false;
		if (config.ReceiveBufferSize == 0) return false;
		if (config.MaximumInboundBytesPerConnection == 0 || config.MaximumOutboundBytesPerConnection == 0) return false;
		if (config.MaximumCommandQueueEntries == 0 || config.MaximumCommandQueueBytes == 0) return false;
		if (config.MaximumEventQueueEntries == 0 || config.MaximumEventQueueBytes == 0) return false;
		if (config.MaximumTotalOutboundBytes == 0 || config.MaximumTotalInboundEventBytes == 0) return false;
		if (config.ReceiveBufferSize > config.MaximumInboundBytesPerConnection) return false;
		if (config.ReceiveBufferSize > config.MaximumEventQueueBytes) return false;
		if (config.MaximumOutboundBytesPerConnection > config.MaximumTotalOutboundBytes) return false;
		if (config.MaximumInboundBytesPerConnection > config.MaximumTotalInboundEventBytes) return false;
		return true;
	}

	Aurore::Network::NetworkError MapCommandPushFailure(Aurore::Network::Detail::QueuePushResult result) noexcept {
		using Aurore::Network::NetworkError;
		using Aurore::Network::Detail::QueuePushResult;
		switch (result) {
		case QueuePushResult::Closed: return NetworkError::CommandQueueClosed;
		case QueuePushResult::EntryLimitExceeded:
		case QueuePushResult::ByteLimitExceeded: return NetworkError::CommandQueueLimitExceeded;
		case QueuePushResult::Queued: break;
		}
		return NetworkError::BackendFailure;
	}
}

namespace Aurore::Network {
	namespace {
		enum class LifecycleState : std::uint8_t {
			Uninitialized,
			Initializing,
			Initialized,
			Starting,
			Running,
			Stopping,
			ShuttingDown,
		};

		bool IsInitializedState(LifecycleState state) noexcept {
			switch (state) {
			case LifecycleState::Initialized:
			case LifecycleState::Starting:
			case LifecycleState::Running:
			case LifecycleState::Stopping: return true;
			case LifecycleState::Uninitialized:
			case LifecycleState::Initializing:
			case LifecycleState::ShuttingDown: return false;
			}
			return false;
		}
	}

	struct NetworkManager::Impl final {
		mutable std::mutex StateMutex;
		LifecycleState State{ LifecycleState::Uninitialized };
		NetworkConfiguration Config;
		std::optional<NetworkEndpoint> BoundEndpoint;

		/* Resources must outlive the queues and backend because their stored reservations reference it. */
		Detail::NetworkResourceLedger Resources;
		Detail::NetworkCommandQueue Commands;
		Detail::NetworkEventQueue Events;
		std::unique_ptr<Detail::NetworkBackend> Backend;

		/* Used only by tests. Initialize() consumes this backend instead of calling CreateNetworkBackend(). */
		std::unique_ptr<Detail::NetworkBackend> PendingTestBackend;
	};

	NetworkManager::NetworkManager() : m_Impl(std::make_unique<Impl>()) {}

	NetworkManager::~NetworkManager() noexcept {
		Shutdown();
	}

	NetworkResult<void> NetworkManager::Initialize(NetworkConfiguration config) {
		if (!ValidateConfiguration(config)) return std::unexpected(NetworkError::InvalidConfiguration);

		std::unique_ptr<Detail::NetworkBackend> backend;
		{
			std::scoped_lock lock(m_Impl->StateMutex);
			if (m_Impl->State != LifecycleState::Uninitialized) return std::unexpected(NetworkError::AlreadyInitialized);

			m_Impl->State = LifecycleState::Initializing;
			m_Impl->BoundEndpoint.reset();
			m_Impl->Commands.Close();
			m_Impl->Events.Close();
			m_Impl->Commands.Clear();
			m_Impl->Events.Clear();

			const bool resources_reset = m_Impl->Resources.Reset(Detail::NetworkResourceLedger::Limits{
				.MaximumOutboundBytesPerConnection = config.MaximumOutboundBytesPerConnection,
				.MaximumInboundEventBytesPerConnection = config.MaximumInboundBytesPerConnection,
				.MaximumTotalOutboundBytes = config.MaximumTotalOutboundBytes,
				.MaximumTotalInboundEventBytes = config.MaximumTotalInboundEventBytes,
			});
			if (!resources_reset) {
				m_Impl->State = LifecycleState::Uninitialized;
				return std::unexpected(NetworkError::BackendFailure);
			}

			m_Impl->Commands.Reset(Detail::QueueLimits{ .MaximumEntries = config.MaximumCommandQueueEntries, .MaximumBytes = config.MaximumCommandQueueBytes });
			m_Impl->Events.Reset(Detail::QueueLimits{ .MaximumEntries = config.MaximumEventQueueEntries, .MaximumBytes = config.MaximumEventQueueBytes });
			if (m_Impl->PendingTestBackend) backend = std::move(m_Impl->PendingTestBackend);
		}

		auto fail_initialization = [this](NetworkError error) -> NetworkResult<void> {
			std::scoped_lock lock(m_Impl->StateMutex);
			m_Impl->Commands.Close();
			m_Impl->Events.Close();
			m_Impl->Commands.Clear();
			m_Impl->Events.Clear();
			m_Impl->Config = {};
			m_Impl->BoundEndpoint.reset();
			m_Impl->State = LifecycleState::Uninitialized;
			return std::unexpected(error);
		};

		if (!backend) {
			auto backend_result = Detail::CreateNetworkBackend(config.Backend);
			if (!backend_result) return fail_initialization(backend_result.error());
			backend = std::move(*backend_result);
		}

		if (!backend) return fail_initialization(NetworkError::BackendUnavailable);

		auto initialize_result = backend->Initialize(config, m_Impl->Commands, m_Impl->Events, m_Impl->Resources);
		if (!initialize_result) {
			backend->Shutdown();
			return fail_initialization(initialize_result.error());
		}

		{
			std::scoped_lock lock(m_Impl->StateMutex);
			m_Impl->Config = std::move(config);
			m_Impl->Backend = std::move(backend);
			m_Impl->BoundEndpoint.reset();
			m_Impl->State = LifecycleState::Initialized;
		}
		return {};
	}

	NetworkResult<NetworkEndpoint> NetworkManager::Start() {
		Detail::NetworkBackend* backend{ nullptr };
		{
			std::scoped_lock lock(m_Impl->StateMutex);
			if (m_Impl->State == LifecycleState::Uninitialized) return std::unexpected(NetworkError::NotInitialized);
			if (m_Impl->State == LifecycleState::Running || m_Impl->State == LifecycleState::Starting) return std::unexpected(NetworkError::AlreadyRunning);
			if (m_Impl->State != LifecycleState::Initialized) return std::unexpected(NetworkError::NotRunning);

			m_Impl->Commands.Clear();
			m_Impl->Events.Clear();
#if defined(_DEBUG)
			const auto resources = m_Impl->Resources.GetSnapshot();
			assert(resources.TotalOutboundBytes == 0 && resources.TotalInboundEventBytes == 0 && resources.ActiveConnections == 0);
#endif
			m_Impl->State = LifecycleState::Starting;
			backend = m_Impl->Backend.get();
		}

		auto start_result = backend->Start();
		{
			std::scoped_lock lock(m_Impl->StateMutex);
			if (!start_result) {
				m_Impl->BoundEndpoint.reset();
				m_Impl->State = LifecycleState::Initialized;
				return std::unexpected(start_result.error());
			}
			m_Impl->BoundEndpoint = *start_result;
			m_Impl->State = LifecycleState::Running;
		}
		return *start_result;
	}

	void NetworkManager::Stop() noexcept {
		Detail::NetworkBackend* backend{ nullptr };
		{
			std::scoped_lock lock(m_Impl->StateMutex);
			if (m_Impl->State != LifecycleState::Running) return;
			m_Impl->State = LifecycleState::Stopping;
			backend = m_Impl->Backend.get();
			const auto result = m_Impl->Commands.Push(Detail::StopCommand{});
			if (result == Detail::QueuePushResult::Queued) backend->NotifyCommandAvailable();
		}

		backend->Stop();

		{
			std::scoped_lock lock(m_Impl->StateMutex);
			m_Impl->Commands.Clear();
			m_Impl->BoundEndpoint.reset();
			m_Impl->State = LifecycleState::Initialized;
		}
	}

	void NetworkManager::Shutdown() noexcept {
		Stop();

		Detail::NetworkBackend* backend{ nullptr };
		{
			std::scoped_lock lock(m_Impl->StateMutex);
			if (m_Impl->State == LifecycleState::Uninitialized) return;
			m_Impl->State = LifecycleState::ShuttingDown;
			backend = m_Impl->Backend.get();
		}

		if (backend != nullptr) backend->Shutdown();

		{
			std::scoped_lock lock(m_Impl->StateMutex);
			m_Impl->Backend.reset();
			m_Impl->Commands.Close();
			m_Impl->Events.Close();
			m_Impl->Commands.Clear();
			m_Impl->Events.Clear();
#if defined(_DEBUG)
			const auto resources = m_Impl->Resources.GetSnapshot();
			assert(resources.TotalOutboundBytes == 0 && resources.TotalInboundEventBytes == 0 && resources.ActiveConnections == 0 && resources.TrackedConnections == 0);
#endif
			m_Impl->Config = {};
			m_Impl->BoundEndpoint.reset();
			m_Impl->State = LifecycleState::Uninitialized;
		}
	}

	std::vector<NetworkEvent> NetworkManager::DrainEvents() {
		auto queued_events = m_Impl->Events.Drain();
		std::vector<NetworkEvent> events;
		events.reserve(queued_events.size());
		for (auto& queued : queued_events) events.push_back(std::move(queued.Event));
		return events;
	}

	NetworkResult<void> NetworkManager::QueueSend(ConnectionId connection, Aurore::Util::ByteBuffer data) {
		std::scoped_lock lock(m_Impl->StateMutex);
		if (m_Impl->State == LifecycleState::Uninitialized) return std::unexpected(NetworkError::NotInitialized);
		if (m_Impl->State != LifecycleState::Running) return std::unexpected(NetworkError::NotRunning);
		if (!connection) return std::unexpected(NetworkError::InvalidConnectionId);
		if (data.Empty()) return std::unexpected(NetworkError::EmptyPayload);

		auto reservation = m_Impl->Resources.ReserveOutbound(connection, data.Size());
		if (!reservation) {
			switch (reservation.error()) {
			case Detail::NetworkResourceLedger::ReserveError::InvalidConnection: return std::unexpected(NetworkError::InvalidConnectionId);
			case Detail::NetworkResourceLedger::ReserveError::PerConnectionLimitExceeded:
			case Detail::NetworkResourceLedger::ReserveError::GlobalLimitExceeded: return std::unexpected(NetworkError::OutboundLimitExceeded);
			}
			return std::unexpected(NetworkError::BackendFailure);
		}

		Detail::NetworkCommand command{ Detail::SendCommand{ .Connection = connection, .Data = std::move(data), .OutboundReservation = std::move(*reservation) } };
		const auto result = m_Impl->Commands.Push(std::move(command));
		if (result != Detail::QueuePushResult::Queued) return std::unexpected(MapCommandPushFailure(result));
		m_Impl->Backend->NotifyCommandAvailable();
		return {};
	}

	NetworkResult<void> NetworkManager::CloseAfterFlush(ConnectionId connection, ConnectionCloseReason reason) {
		std::scoped_lock lock(m_Impl->StateMutex);
		if (m_Impl->State == LifecycleState::Uninitialized) return std::unexpected(NetworkError::NotInitialized);
		if (m_Impl->State != LifecycleState::Running) return std::unexpected(NetworkError::NotRunning);
		if (!connection) return std::unexpected(NetworkError::InvalidConnectionId);
		const auto result = m_Impl->Commands.Push(Detail::CloseAfterFlushCommand{ .Connection = connection, .Reason = reason });
		if (result != Detail::QueuePushResult::Queued) return std::unexpected(MapCommandPushFailure(result));
		m_Impl->Backend->NotifyCommandAvailable();
		return {};
	}

	NetworkResult<void> NetworkManager::CloseImmediately(ConnectionId connection, ConnectionCloseReason reason) {
		std::scoped_lock lock(m_Impl->StateMutex);
		if (m_Impl->State == LifecycleState::Uninitialized) return std::unexpected(NetworkError::NotInitialized);
		if (m_Impl->State != LifecycleState::Running) return std::unexpected(NetworkError::NotRunning);
		if (!connection) return std::unexpected(NetworkError::InvalidConnectionId);
		const auto result = m_Impl->Commands.Push(Detail::CloseImmediatelyCommand{ .Connection = connection, .Reason = reason });
		if (result != Detail::QueuePushResult::Queued) return std::unexpected(MapCommandPushFailure(result));
		m_Impl->Backend->NotifyCommandAvailable();
		return {};
	}

	NetworkResult<void> NetworkManager::ResumeReceive(ConnectionId connection) {
		std::scoped_lock lock(m_Impl->StateMutex);
		if (m_Impl->State == LifecycleState::Uninitialized) return std::unexpected(NetworkError::NotInitialized);
		if (m_Impl->State != LifecycleState::Running) return std::unexpected(NetworkError::NotRunning);
		if (!connection) return std::unexpected(NetworkError::InvalidConnectionId);
		const auto result = m_Impl->Commands.Push(Detail::ResumeReceiveCommand{ .Connection = connection });
		if (result != Detail::QueuePushResult::Queued) return std::unexpected(MapCommandPushFailure(result));
		m_Impl->Backend->NotifyCommandAvailable();
		return {};
	}

	bool NetworkManager::IsInitialized() const noexcept {
		std::scoped_lock lock(m_Impl->StateMutex);
		return IsInitializedState(m_Impl->State);
	}

	bool NetworkManager::IsRunning() const noexcept {
		std::scoped_lock lock(m_Impl->StateMutex);
		return m_Impl->State == LifecycleState::Running;
	}

	std::optional<NetworkConfiguration> NetworkManager::GetConfiguration() const {
		std::scoped_lock lock(m_Impl->StateMutex);
		if (!IsInitializedState(m_Impl->State)) return std::nullopt;
		return m_Impl->Config;
	}

	std::optional<NetworkEndpoint> NetworkManager::GetBoundEndpoint() const {
		std::scoped_lock lock(m_Impl->StateMutex);
		return m_Impl->BoundEndpoint;
	}

	bool NetworkManager::InstallBackendForTesting(std::unique_ptr<Detail::NetworkBackend> backend) {
		if (!backend) return false;
		std::scoped_lock lock(m_Impl->StateMutex);
		if (m_Impl->State != LifecycleState::Uninitialized) return false;
		m_Impl->PendingTestBackend = std::move(backend);
		return true;
	}

	NetworkResourceSnapshot NetworkManager::GetResourceSnapshotForTesting() const noexcept {
		std::scoped_lock lock(m_Impl->StateMutex);
		return m_Impl->Resources.GetSnapshot();
	}

	NetworkQueueSnapshot NetworkManager::GetCommandQueueSnapshotForTesting() const noexcept {
		std::scoped_lock lock(m_Impl->StateMutex);
		return m_Impl->Commands.GetSnapshot();
	}

	NetworkQueueSnapshot NetworkManager::GetEventQueueSnapshotForTesting() const noexcept {
		std::scoped_lock lock(m_Impl->StateMutex);
		return m_Impl->Events.GetSnapshot();
	}
}

