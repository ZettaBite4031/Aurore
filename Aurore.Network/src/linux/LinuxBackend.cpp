#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "LinuxBackend.hpp"

#include <cerrno>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace Aurore::Network::Detail {
	std::unique_ptr<NetworkBackend> CreateLinuxEpollBackend() {
		return std::make_unique<Linux::LinuxNetworkBackend>();
	}
}

namespace Aurore::Network::Detail::Linux {
	LinuxNetworkBackend::~LinuxNetworkBackend() noexcept {
		Shutdown();
	}

	NetworkResult<void> LinuxNetworkBackend::Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) {
		if (m_Initialized) {
			return std::unexpected(NetworkError::AlreadyInitialized);
		}

		UniqueFd epoll = CreateEpollInstance();
		if (!epoll) {
			return std::unexpected(NetworkError::BackendFailure);
		}

		UniqueFd command_event = CreateEventCounter();
		if (!command_event) {
			return std::unexpected(NetworkError::BackendFailure);
		}

		if (!AddEpollInterest(epoll.Get(), command_event.Get(), EPOLLIN, CommandEventToken)) {
			return std::unexpected(NetworkError::BackendFailure);
		}

		m_Config = config;
		m_Commands = &commands;
		m_Events = &events;
		m_Resources = &resources;

		m_Epoll = std::move(epoll);
		m_CommandEvent = std::move(command_event);
		m_NextConnectionId = 1;

		m_StopRequested.store(false, std::memory_order_release);
		m_Started.store(false, std::memory_order_release);
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;
		m_Initialized = true;

		return {};
	}

	NetworkResult<NetworkEndpoint> LinuxNetworkBackend::Start() {
		if (!m_Initialized) {
			return std::unexpected(NetworkError::NotInitialized);
		}

		if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable()) {
			return std::unexpected(NetworkError::AlreadyRunning);
		}

		auto listener_result = CreateListener();
		if (!listener_result) return std::unexpected(listener_result.error());

		m_BoundEndpoint = *listener_result;
		m_StopRequested.store(false, std::memory_order_release);
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;

		auto worker_result = StartWorker();
		if (!worker_result) {
			m_Listener.Reset();
			m_BoundEndpoint.reset();
			return std::unexpected(worker_result.error());
		}

		return *m_BoundEndpoint;
	}

	void LinuxNetworkBackend::NotifyCommandAvailable() noexcept {
		if (!m_Started.load(std::memory_order_acquire)) {
			return;
		}

		if (WakeEventCounter(m_CommandEvent.Get())) {
			return;
		}

		m_StopRequested.store(true, std::memory_order_release);
	}

	void LinuxNetworkBackend::Stop() noexcept {
		StopWorker();

		try {
			BeginShutdown();
		} catch (...) {
			if (m_Resources != nullptr) {
				for (const auto& [connection, state] : m_Connections) {
					(void)state;
					m_Resources->DeactivateConnection(connection);
				}
			}

			m_Connections.clear();
			m_Listener.Reset();
		}

		m_BoundEndpoint.reset();
		m_ShuttingDown = false;
	}

	void LinuxNetworkBackend::Shutdown() noexcept {
		Stop();

		m_Connections.clear();
		m_Listener.Reset();
		m_BoundEndpoint.reset();
		m_CommandEvent.Reset();
		m_Epoll.Reset();

		m_Commands = nullptr;
		m_Events = nullptr;
		m_Resources = nullptr;

		m_Config = {};
		m_Initialized = false;
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;
		m_NextConnectionId = 1;

		m_StopRequested.store(false, std::memory_order_release);
		m_Started.store(false, std::memory_order_release);

		{
			std::scoped_lock lock(m_StartupMutex);
			m_StartupComplete = false;
			m_StartupError.reset();
		}
	}

	NetworkResult<NetworkEndpoint> LinuxNetworkBackend::CreateListener() {
		m_Listener.Reset();
		m_BoundEndpoint.reset();

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		const std::string service = std::to_string(m_Config.Port);
		const char* node = m_Config.BindAddress == "*" ? nullptr : m_Config.BindAddress.c_str();

		addrinfo* raw_addresses{ nullptr };
		const int resolve_result = ::getaddrinfo(node, service.c_str(), &hints, &raw_addresses);
		if (resolve_result != 0) {
			EmitFailure(NetworkError::BackendFailure, "Failed to resolve network bind address: " + std::string(::gai_strerror(resolve_result)), true);
			return std::unexpected(NetworkError::BackendFailure);
		}

		UniqueAddressInfo addresses(raw_addresses);

		for (addrinfo* address = addresses.get(); address != nullptr; address = address->ai_next) {
			UniqueFd candidate(::socket(address->ai_family, address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, address->ai_protocol));
			if (!candidate) continue;
			if (!SetReuseAddress(candidate.Get())) continue;
			if (::bind(candidate.Get(), address->ai_addr, address->ai_addrlen) != 0) continue;
			if (::listen(candidate.Get(), SOMAXCONN) != 0) continue;

			sockaddr_storage local_address{};
			socklen_t local_length{ static_cast<socklen_t>(sizeof(local_address)) };
			if (::getsockname(candidate.Get(), reinterpret_cast<sockaddr*>(&local_address), &local_length) != 0) continue;

			auto endpoint = MakeNetworkEndpoint(reinterpret_cast<const sockaddr*>(&local_address), local_length);
			if (!endpoint) continue;

			if (!AddEpollInterest(m_Epoll.Get(), candidate.Get(), ListenerEvents, ListenerEventToken)) continue;

			m_Listener = std::move(candidate);
			return *endpoint;
		}

		EmitFailure(NetworkError::BackendFailure, "Unable to bind and listen on " + m_Config.BindAddress + ":" + std::to_string(m_Config.Port), true);
		return std::unexpected(NetworkError::BackendFailure);
	}

	NetworkResult<void> LinuxNetworkBackend::StartWorker() {
		if (!m_Initialized) {
			return std::unexpected(NetworkError::NotInitialized);
		}

		if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable()) {
			return std::unexpected(NetworkError::AlreadyRunning);
		}

		if (!m_Epoll || !m_CommandEvent) {
			return std::unexpected(NetworkError::BackendFailure);
		}

		{
			std::scoped_lock lock(m_StartupMutex);

			m_StartupComplete = false;
			m_StartupError.reset();
		}

		m_StopRequested.store(false, std::memory_order_release);
		m_WorkerFailureReported = false;
		m_Started.store(true, std::memory_order_release);

		try {
			m_Worker = std::thread(&LinuxNetworkBackend::WorkerMain, this);
		} catch (...) {
			m_Started.store(false, std::memory_order_release);
			return std::unexpected(NetworkError::BackendFailure);
		}

		std::optional<NetworkError> startup_error;

#ifndef NDEBUG
		bool startup_timed_out{ false };
#endif

		{
			std::unique_lock lock(m_StartupMutex);

#ifndef NDEBUG
			constexpr auto StartupTimeout = std::chrono::seconds{ 5 };
			const bool completed = m_StartupCondition.wait_for(lock, StartupTimeout, [this] { return m_StartupComplete; });
			if (!completed) {
				startup_error = NetworkError::BackendFailure;
				startup_timed_out = true;
			} else {
				startup_error = m_StartupError;
			}
#else
			m_StartupCondition.wait(lock, [this] { return m_StartupComplete; });
			startup_error = m_StartupError;
#endif
		}

#ifndef NDEBUG
		if (startup_timed_out) {
			m_StopRequested.store(true, std::memory_order_release);
			[[maybe_unused]] const bool woke = WakeEventCounter(m_CommandEvent.Get());
		}
#endif

		if (startup_error.has_value()) {
			if (m_Worker.joinable()) m_Worker.join();

			m_Started.store(false, std::memory_order_release);
			return std::unexpected(*startup_error);
		}

		return {};
	}

	void LinuxNetworkBackend::StopWorker() noexcept {
		if (!m_Worker.joinable()) {
			m_Started.store(false, std::memory_order_release);
			return;
		}

		m_StopRequested.store(true, std::memory_order_release);

		[[maybe_unused]] const bool woke = WakeEventCounter(m_CommandEvent.Get());

		m_Worker.join();
		m_Started.store(false, std::memory_order_release);
	}

	void LinuxNetworkBackend::WorkerMain() noexcept {
		SignalStartup(std::nullopt);
		WorkerEventBuffer events{};

		while (!m_StopRequested.load(std::memory_order_acquire)) {
			try {
				const int count = ::epoll_wait(m_Epoll.Get(), events.data(), static_cast<int>(events.size()), WaitIndefinitely);
				if (count < 0) {
					const int error = errno;
					if (error == EINTR) continue;
					HandleWorkerFailure(error, "epoll_wait");
					break;
				}

				for (int i = 0; i < count; i++) {
					const epoll_event& event = events[static_cast<std::size_t>(i)];
					const std::uint64_t token = event.data.u64;

					if (token == CommandEventToken) {
						if (!DrainEventCounter(m_CommandEvent.Get())) {
							HandleWorkerFailure(errno, "eventfd read");
							break;
						}

						/* Command dispatch arrives in step 3. */
						continue;
					}

					if (token == ListenerEventToken) {
						HandleListenerEvent(event.events);
						continue;
					}

					HandleConnectionEvent(ConnectionId{ .Value = token }, event.events);
				}
			} catch (const std::exception& ex) {
				HandleWorkerException(ex.what());
				break;
			} catch (...) {
				HandleWorkerException("Unhandled non-standard exception in the epoll worker");
				break;
			}
		}

		SignalStartupFailureNoexcept();
		m_Started.store(false, std::memory_order_release);
	}

	void LinuxNetworkBackend::HandleListenerEvent(std::uint32_t events) {
		if (m_ShuttingDown) return;

		if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
			const int error = GetSocketError(m_Listener.Get());
			HandleWorkerFailure(error != 0 ? error : EIO, "listener readiness");
			return;
		}

		if ((events & EPOLLIN) != 0) AcceptConnections();
	}

	void LinuxNetworkBackend::AcceptConnections() {
		while (!m_ShuttingDown && !m_StopRequested.load(std::memory_order_acquire)) {
			sockaddr_storage remote_address{};
			socklen_t remote_length{ static_cast<socklen_t>(sizeof(remote_address)) };

			const int accepted_fd = ::accept4(m_Listener.Get(), reinterpret_cast<sockaddr*>(&remote_address), &remote_length, SOCK_NONBLOCK | SOCK_CLOEXEC);
			if (accepted_fd < 0) {
				const int error = errno;
				if (error == EAGAIN || error == EWOULDBLOCK) return;
				if (IsRetryableAcceptError(error)) continue;
				HandleWorkerFailure(error, "accept4");
				return;
			}

			UniqueFd accepted(accepted_fd);

			if (m_Connections.size() >= m_Config.MaximumConnections) {
				SetAbortiveClose(accepted.Get());
				continue;
			}

			if (!SetTcpNoDelay(accepted.Get())) {
				EmitFailure(NetworkError::BackendFailure, "Failed to enable TCP_NODELAY on an accepted socket: " + FormatSystemError(errno), false);
				continue;
			}

			sockaddr_storage local_address{};
			socklen_t local_length{ static_cast<socklen_t>(sizeof(local_address)) };
			if (::getsockname(accepted.Get(), reinterpret_cast<sockaddr*>(&local_address), &local_length) != 0) {
				EmitFailure(NetworkError::BackendFailure, "Failed to query the local endpoint of an accepted socket: " + FormatSystemError(errno), false);
				continue;
			}

			auto local_endpoint = MakeNetworkEndpoint(reinterpret_cast<const sockaddr*>(&local_address), local_length);
			auto remote_endpoint = MakeNetworkEndpoint(reinterpret_cast<const sockaddr*>(&remote_address), remote_length);
			if (!local_endpoint || !remote_endpoint) {
				EmitFailure(NetworkError::BackendFailure, "Failed to decode accepted socket endpoints", false);
				continue;
			}

			const ConnectionId connection = AllocateConnectioNId();
			auto [iterator, inserted] = m_Connections.try_emplace(connection, connection, std::move(accepted), *local_endpoint, *remote_endpoint);
			if (!inserted) {
				HandleWorkerFailure(EEXIST, "connection registration");
				return;
			}

			if (m_Resources == nullptr || !m_Resources->RegisterConnection(connection)) {
				m_Connections.erase(iterator);
				HandleWorkerFailure(EINVAL, "connection resource registration");
				return;
			}

			if (!AddEpollInterest(m_Epoll.Get(), iterator->second.Socket.Get(), DormantConnectionEvents, connection.Value)) {
				const int error = errno;
				m_Resources->DeactivateConnection(connection);
				m_Connections.erase(iterator);
				HandleWorkerFailure(error, "client epoll registration");
				return;
			}

			const auto event_result = PushEvent(ConnectionOpenedEvent{ .Connection = connection, .LocalEndpoint = *local_endpoint, .RemoteEndpoint = *remote_endpoint });
			if (event_result != QueuePushResult::Queued) {
				CloseConnection(connection, ConnectionCloseReason::BackendFailure, "Network event queue could not accept a connection-opened event", true);
				BeginShutdown();
				return;
			}
		}
	}

	void LinuxNetworkBackend::HandleConnectionEvent(ConnectionId connection, std::uint32_t events) {
		const auto iterator = m_Connections.find(connection);

		/* A stale epoll token is harmless even if Linux has reused the old fd.*/
		if (iterator == m_Connections.end()) return;

		if ((events & EPOLLERR) != 0) {
			const int error = GetSocketError(iterator->second.Socket.Get());
			CloseConnection(connection, ConnectionCloseReason::TransportError, "SOcket readiness reported an error: " + FormatSystemError(error != 0 ? error : EIO), true);
			return;
		}

		if ((events & (EPOLLRDHUP| EPOLLHUP)) != 0) {
			CloseConnection(connection, ConnectionCloseReason::RemoteClosed, "Remote peer closed the connection", false);
		}
	}

	ConnectionId LinuxNetworkBackend::AllocateConnectioNId() noexcept {
		while (true) {
			ConnectionId candidate{ .Value = m_NextConnectionId++ };
			if (m_NextConnectionId == 0 || m_NextConnectionId == ListenerEventToken)
				m_NextConnectionId = 1;

			if (!candidate || candidate.Value == ListenerEventToken) continue;
			if (!m_Connections.contains(candidate)) return candidate;
		}
	}

	void LinuxNetworkBackend::CloseConnection(ConnectionId connection, ConnectionCloseReason reason, std::string detail, bool abortive) {
		auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return;

		auto& state = iterator->second;
		if (state.Socket) {
			[[maybe_unused]] const bool removed = RemoveEpollInterest(m_Epoll.Get(), state.Socket.Get());
			if (m_Resources != nullptr) m_Resources->DeactivateConnection(connection);
			if (abortive) SetAbortiveClose(state.Socket.Get());
			state.Socket.Reset();
		}

		QueuePushResult event_result{ QueuePushResult::Queued };
		if (!state.CloseEventEmitted) {
			state.CloseEventEmitted = true;
			event_result = PushEvent(ConnectionClosedEvent{ .Connection = connection, .Reason = reason, .Detail = std::move(detail), });
		}

		m_Connections.erase(iterator);

		if (event_result != QueuePushResult::Queued && !m_ShuttingDown)
			BeginShutdown();
	}

	void LinuxNetworkBackend::BeginShutdown() {
		if (m_ShuttingDown) return;
		m_ShuttingDown = true;
		m_Listener.Reset();

		while (!m_Connections.empty()) {
			const ConnectionId connection = m_Connections.begin()->first;
			CloseConnection(connection, ConnectionCloseReason::ServerStopping, "Network backend is shutting down", true);
		}
	}

	QueuePushResult LinuxNetworkBackend::PushEvent(NetworkEvent event, NetworkResourceLedger::Reservation reservation) {
		if (m_Events == nullptr) return QueuePushResult::Closed;
		return m_Events->Push(QueuedNetworkEvent{ .Event = std::move(event), .InboundReservation = std::move(reservation) });
	}

	void LinuxNetworkBackend::HandleWorkerFailure(int error, std::string_view operation) noexcept {
		m_StopRequested.store(true, std::memory_order_release);
		SignalStartupFailureNoexcept();

		if (!m_WorkerFailureReported) {
			m_WorkerFailureReported = true;
			try {
				std::string message{ "Linux epoll worker failure in " };
				message.append(operation);
				if (error != 0) {
					message += ": ";
					message += FormatSystemError(error);
				}
				EmitFailure(NetworkError::BackendFailure, std::move(message), true);
			}
			catch (...) {}
		}

		try {
			BeginShutdown();
		}
		catch (...) {}
	}

	void LinuxNetworkBackend::HandleWorkerException(std::string_view message) noexcept {
		m_StopRequested.store(true, std::memory_order_release);
		SignalStartupFailureNoexcept();

		if (!m_WorkerFailureReported) {
			m_WorkerFailureReported = true;
			try {
				std::string detail{ "Unhandled exception in epoll worker" };
				if (!message.empty()) {
					detail += ": ";
					detail.append(message);
				}
				EmitFailure(NetworkError::BackendFailure, std::move(detail), true);
			}
			catch (...) {}
		}

		try {
			BeginShutdown();
		}
		catch (...) {}
	}

	void LinuxNetworkBackend::EmitFailure(NetworkError error, std::string message, bool fatal) noexcept {
		if (m_Events == nullptr) return;
		try {
			[[maybe_unused]] const auto result = m_Events->Push(QueuedNetworkEvent{ .Event = NetworkFailureEvent{ .Error = error, .Message = std::move(message), .Fatal = fatal, }, .InboundReservation = {}, });
		} catch (...) {}
	}

	void LinuxNetworkBackend::SignalStartup(std::optional<NetworkError> error) noexcept {
		try {
			{
				std::scoped_lock lock(m_StartupMutex);
				if (m_StartupComplete) return;
				m_StartupError = error;
				m_StartupComplete = true;
			}
			m_StartupCondition.notify_all();
		} catch (...) {
			m_StopRequested.store(true, std::memory_order_release);
			m_StartupCondition.notify_all();
		}
	}

	void LinuxNetworkBackend::SignalStartupFailureNoexcept() noexcept {
		try {
			bool notify{ false };
			{
				std::scoped_lock lock(m_StartupMutex);
				if (!m_StartupComplete) {
					m_StartupError = NetworkError::BackendFailure;
					m_StartupComplete = true;
					notify = true;
				}
			}

			if (notify) m_StartupCondition.notify_all();
		} catch (...) {
			m_StartupCondition.notify_all();
		}
	}

	bool LinuxNetworkBackend::IsStartupComplete() const noexcept {
		try {
			std::scoped_lock lock(m_StartupMutex);
			return m_StartupComplete;
		} catch (...) {
			return false;
		}
	}
}

