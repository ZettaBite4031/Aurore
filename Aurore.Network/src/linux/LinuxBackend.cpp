#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "LinuxBackend.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
		if (m_Initialized)
			return std::unexpected(NetworkError::AlreadyInitialized);

		if (config.ReceiveBufferSize == 0 || config.ReceiveBufferSize > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()))
			return std::unexpected(NetworkError::InvalidConfiguration);

		UniqueFd epoll = CreateEpollInstance();
		if (!epoll)
			return std::unexpected(NetworkError::BackendFailure);

		UniqueFd command_event = CreateEventCounter();
		if (!command_event)
			return std::unexpected(NetworkError::BackendFailure);

		if (!AddEpollInterest(epoll.Get(), command_event.Get(), EPOLLIN, CommandEventToken))
			return std::unexpected(NetworkError::BackendFailure);

		m_Config = config;
		m_Commands = &commands;
		m_Events = &events;
		m_Resources = &resources;

		m_Epoll = std::move(epoll);
		m_CommandEvent = std::move(command_event);

		m_NextConnectionId = 1;
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;

		m_StopRequested.store(false, std::memory_order_release);
		m_Started.store(false, std::memory_order_release);

		m_Initialized = true;
		return {};
	}

	NetworkResult<NetworkEndpoint> LinuxNetworkBackend::Start() {
		if (!m_Initialized)
			return std::unexpected(NetworkError::NotInitialized);

		if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable())
			return std::unexpected(NetworkError::AlreadyRunning);

		auto listener_result = CreateListener();
		if (!listener_result)
			return std::unexpected(listener_result.error());

		m_BoundEndpoint = *listener_result;
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;

		m_StopRequested.store(false, std::memory_order_release);

		auto worker_result = StartWorker();
		if (!worker_result) {
			m_Listener.Reset();
			m_BoundEndpoint.reset();
			return std::unexpected(worker_result.error());
		}

		return *m_BoundEndpoint;
	}

	void LinuxNetworkBackend::NotifyCommandAvailable() noexcept {
		if (!m_Started.load(std::memory_order_acquire)) return;
		if (WakeEventCounter(m_CommandEvent.Get())) return;
		m_StopRequested.store(true, std::memory_order_release);
	}

	void LinuxNetworkBackend::Stop() noexcept {
		StopWorker();

		try {
			BeginShutdown();
		}
		catch (...) {
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
			if (::getsockname(candidate.Get(), reinterpret_cast<sockaddr*>(&local_address), &local_length) != 0)
				continue;

			auto endpoint = MakeNetworkEndpoint(reinterpret_cast<const sockaddr*>(&local_address), local_length);
			if (!endpoint) continue;
			if (!AddEpollInterest(m_Epoll.Get(), candidate.Get(), ListenerEvents, ListenerEventToken))
				continue;

			m_Listener = std::move(candidate);
			return *endpoint;
		}

		EmitFailure(NetworkError::BackendFailure,
			"Unable to bind and listen on "
				+ m_Config.BindAddress
				+ ":"
				+ std::to_string(m_Config.Port),
			true);

		return std::unexpected(NetworkError::BackendFailure);
	}

	NetworkResult<void> LinuxNetworkBackend::StartWorker() {
		if (!m_Initialized)
			return std::unexpected(NetworkError::NotInitialized);

		if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable())
			return std::unexpected(NetworkError::AlreadyRunning);

		if (!m_Epoll || !m_CommandEvent || !m_Listener || !m_BoundEndpoint)
			return std::unexpected(NetworkError::BackendFailure);

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
		}
		catch (...) {
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
			}
			else startup_error = m_StartupError;
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
			if (m_Worker.joinable())
				m_Worker.join();

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
				const int count = ::epoll_wait( m_Epoll.Get(), events.data(), static_cast<int>(events.size()), WaitIndefinitely);
				if (count < 0) {
					const int error = errno;
					if (error == EINTR) continue;
					HandleWorkerFailure(error, "epoll_wait");
					break;
				}

				for (int index{ 0 }; index < count; ++index) {
					const epoll_event& event = events[static_cast<std::size_t>(index)];
					const std::uint64_t token = event.data.u64;

					if (token == CommandEventToken) {
						if (!DrainEventCounter(m_CommandEvent.Get())) {
							HandleWorkerFailure(errno, "eventfd read");
							break;
						}

						DrainCommands();
						if (m_StopRequested.load(std::memory_order_acquire)) break;
						continue;
					}

					if (token == ListenerEventToken) {
						HandleListenerEvent(event.events);
						if (m_StopRequested.load(std::memory_order_acquire)) break;
						continue;
					}

					HandleConnectionEvent(ConnectionId{ .Value = token }, event.events);
					if (m_StopRequested.load(std::memory_order_acquire)) break;
				}
			}
			catch (const std::exception& ex) {
				HandleWorkerException(ex.what());
				break;
			}
			catch (...) {
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

			const ConnectionId connection = AllocateConnectionId();
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

			if (!AddEpollInterest(m_Epoll.Get(), iterator->second.Socket.Get(), BuildConnectionEvents(iterator->second), connection.Value)) {
				const int error = errno;
				m_Resources->DeactivateConnection(connection);
				m_Connections.erase(iterator);
				HandleWorkerFailure(error, "client epoll registration");
				return;
			}

			const auto event_result = PushEvent(ConnectionOpenedEvent{
					.Connection = connection,
					.LocalEndpoint = *local_endpoint,
					.RemoteEndpoint = *remote_endpoint,
				});

			if (event_result != QueuePushResult::Queued) {
				CloseConnection(connection, ConnectionCloseReason::BackendFailure, "Network event queue could not accept a connection-opened event", true);
				BeginShutdown();
				return;
			}
		}
	}
	void LinuxNetworkBackend::HandleConnectionEvent(ConnectionId connection, std::uint32_t events) {
		auto iterator = m_Connections.find(connection);

		/*
			A queued epoll token may outlive the connection that
			produced it. Stable connection IDs make that stale
			event harmless even if Linux has reused the old fd.
		*/
		if (iterator == m_Connections.end()) return;
		if ((events & EPOLLERR) != 0) {
			const int error = GetSocketError(iterator->second.Socket.Get());
			CloseConnection(connection, ConnectionCloseReason::TransportError,
				"Socket readiness reported an error: " + FormatSystemError(error != 0 ? error : EIO),
				true);

			return;
		}

		/*
			Inbound delivery is intentionally a decision boundary.
			If EPOLLIN is present, process at most one receive event
			and return. Any simultaneous write/hangup readiness stays
			level-triggered and will be observed on the next wait.
		*/
		if ((events & EPOLLIN) != 0) {
			ReceiveAvailable(connection);
			return;
		}

		if ((events & EPOLLOUT) != 0) {
			FlushOutbound(connection);
			if (!m_Connections.contains(connection)) return;
		}

		if ((events & EPOLLHUP) != 0) {
			iterator = m_Connections.find(connection);
			if (iterator == m_Connections.end()) return;

			/*
				EPOLLHUP can be reported while unread stream data still
				remains. Preserve the same decision-boundary behavior as
				IOCP: drain that data first, then observe EOF on a later
				receive. If receive delivery is paused, Core must resume it
				before we inspect the stream again.
			*/
			if (iterator->second.CloseMode == ConnectionCloseMode::Open && !iterator->second.ReceivePaused)
				ReceiveAvailable(connection);
		}
	}

	void LinuxNetworkBackend::ReceiveAvailable(ConnectionId connection) {
		auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return;
		auto& state = iterator->second;
		if (!state.Socket || state.CloseMode != ConnectionCloseMode::Open || state.ReceivePaused || m_ShuttingDown)
			return;

		std::vector<std::byte> buffer(m_Config.ReceiveBufferSize);
		while (true) {
			const ssize_t received = ::recv(state.Socket.Get(), buffer.data(), buffer.size(), 0);
			if (received > 0) {
				buffer.resize(static_cast<std::size_t>(received));
				state.ReceivePaused = true;

				if (!UpdateConnectionInterest(connection)) return;
				if (m_Resources == nullptr) {
					CloseConnection(connection, ConnectionCloseReason::BackendFailure, "Network resource ledger is unavailable", true);
					BeginShutdown();
					return;
				}

				auto reservation = m_Resources->ReserveInboundEvent(connection, buffer.size());
				if (!reservation) {
					if (reservation.error() == NetworkResourceLedger::ReserveError::InvalidConnection) {
						CloseConnection(connection, ConnectionCloseReason::BackendFailure, "Inbound accounting referenced an inactive connection", true);
						BeginShutdown();
					}
					else CloseConnection(connection, ConnectionCloseReason::InboundLimitExceeded, "Inbound event memory limit exceeded", true);
					return;
				}

				const auto event_result = PushEvent(BytesReceivedEvent{ .Connection = connection, .Data = std::move(buffer), }, std::move(*reservation));
				if (event_result != QueuePushResult::Queued) {
					CloseConnection(connection, ConnectionCloseReason::BackendFailure,
						event_result == QueuePushResult::Closed
							? "Network event queue was closed"
							: "Network event queue budget was exceeded",
						true);

					BeginShutdown();
				}

				return;
			}

			if (received == 0) {
				CloseConnection(connection, ConnectionCloseReason::RemoteClosed, "Remote peer closed the connection", false);
				return;
			}

			const int error = errno;
			if (error == EINTR) continue;
			if (error == EAGAIN || error == EWOULDBLOCK) return;
			CloseConnection(connection, ConnectionCloseReason::TransportError, "recv failed: " + FormatSystemError(error), true);
			return;
		}
	}

	void LinuxNetworkBackend::FlushOutbound(ConnectionId connection) {
		while (true) {
			auto iterator = m_Connections.find(connection);
			if (iterator == m_Connections.end()) return;
			auto& state = iterator->second;
			if (!state.Socket) return;
			if (state.OutboundQueue.empty()) {
				if (!UpdateConnectionInterest(connection))
					return;

				TryCompleteCloseAfterFlush(connection);
				return;
			}

			auto& outbound = state.OutboundQueue.front();
			const std::size_t remaining = outbound.Remaining();

			if (remaining == 0) {
				state.OutboundQueue.pop_front();
				continue;
			}

			const auto bytes = outbound.Data.Bytes();
			const std::size_t request_size = std::min(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
			const ssize_t sent = ::send(state.Socket.Get(), bytes.data() + outbound.Offset, request_size, MSG_NOSIGNAL);

			if (sent > 0) {
				const std::size_t transferred = static_cast<std::size_t>(sent);
				if (transferred > remaining) {
					CloseConnection(connection, ConnectionCloseReason::BackendFailure, "send returned an invalid transfer length", true);
					return;
				}

				outbound.Offset += transferred;
				outbound.OutboundReservation.Release(transferred);

				if (outbound.Remaining() == 0)
					state.OutboundQueue.pop_front();

				/*
					For now, continue draining until the socket
					would block. If profiling later shows worker
					fairness problems, add a per-dispatch byte budget.
				*/
				continue;
			}

			if (sent == 0) {
				CloseConnection(connection, ConnectionCloseReason::BackendFailure, "send returned zero bytes for a non-empty buffer", true);
				return;
			}

			const int error = errno;
			if (error == EINTR) continue;
			if (error == EAGAIN || error == EWOULDBLOCK) {
				[[maybe_unused]]
				const bool updated = UpdateConnectionInterest(connection);
				return;
			}

			CloseConnection(connection, ConnectionCloseReason::TransportError, "send failed: " + FormatSystemError(error), true);
			return;
		}
	}

	void LinuxNetworkBackend::DrainCommands() {
		if (m_Commands == nullptr) return;
		while (auto command = m_Commands->TryPop()) {
			std::visit([this](auto&& value) { HandleCommand(std::forward<decltype(value)>(value)); }, std::move(*command));
			if (m_StopRequested.load(std::memory_order_acquire)) break;
		}
	}

	void LinuxNetworkBackend::HandleCommand(SendCommand&& command) {
		if (m_ShuttingDown) return;
		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) {
			EmitFailure(NetworkError::InvalidConnectionId, "A send command referenced an unknown connection", false);
			return;
		}

		auto& state = iterator->second;
		if (!state.Socket || state.CloseMode != ConnectionCloseMode::Open)
			return;

		state.OutboundQueue.push_back(OutboundBuffer{
				.Data = std::move(command.Data),
				.OutboundReservation = std::move(command.OutboundReservation),
			});

		FlushOutbound(command.Connection);
	}

	void LinuxNetworkBackend::HandleCommand(const CloseAfterFlushCommand& command) {
		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) return;
		auto& state = iterator->second;
		if (!state.Socket) return;

		state.CloseMode = ConnectionCloseMode::AfterFlush;
		state.RequestedCloseReason = command.Reason;
		state.ReceivePaused = true;

		if (!UpdateConnectionInterest(command.Connection))
			return;

		FlushOutbound(command.Connection);
	}

	void LinuxNetworkBackend::HandleCommand( const CloseImmediatelyCommand& command) {
		CloseConnection(command.Connection, command.Reason, "Connection closed immediately by application", true);
	}

	void LinuxNetworkBackend::HandleCommand( const ResumeReceiveCommand& command) {
		if (m_ShuttingDown) return;
		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) return;
		auto& state = iterator->second;
		if (!state.Socket || state.CloseMode != ConnectionCloseMode::Open || !state.ReceivePaused)
			return;

		state.ReceivePaused = false;

		if (!UpdateConnectionInterest(command.Connection))
			return;
	}

	void LinuxNetworkBackend::HandleCommand(const StopCommand& command) {
		(void)command;
		BeginShutdown();
	}

	std::uint32_t LinuxNetworkBackend::BuildConnectionEvents( const BackendConnection& connection) const noexcept {
		std::uint32_t events{ 0 };
		if (!m_ShuttingDown && connection.CloseMode == ConnectionCloseMode::Open && !connection.ReceivePaused)
			events |= EPOLLIN;

		if (!connection.OutboundQueue.empty())
			events |= EPOLLOUT;

		/*
			EPOLLERR and EPOLLHUP are reported by epoll regardless
			of whether they are present in the requested mask.
		*/
		return events;
	}

	bool LinuxNetworkBackend::UpdateConnectionInterest( ConnectionId connection) {
		auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return false;
		auto& state = iterator->second;
		if (!state.Socket) return false;
		if (ModifyEpollInterest(m_Epoll.Get(), state.Socket.Get(), BuildConnectionEvents(state), connection.Value))
			return true;

		const int error = errno;
		HandleWorkerFailure(error, "client epoll interest update");
		return false;
	}

	void LinuxNetworkBackend::TryCompleteCloseAfterFlush( ConnectionId connection) {
		auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return;
		auto& state = iterator->second;
		if (!state.Socket || state.CloseMode != ConnectionCloseMode::AfterFlush || !state.OutboundQueue.empty())
			return;

		CloseConnection(connection, state.RequestedCloseReason, "Connection closed after flushing outbound data", false);
	}

	ConnectionId LinuxNetworkBackend::AllocateConnectionId() noexcept {
		while (true) {
			ConnectionId candidate{ .Value = m_NextConnectionId++ };

			if (m_NextConnectionId == 0 || m_NextConnectionId == ListenerEventToken)
				m_NextConnectionId = 1;

			if (!candidate || candidate.Value == ListenerEventToken)
				continue;

			if (!m_Connections.contains(candidate))
				return candidate;
		}
	}

	void LinuxNetworkBackend::CloseConnection(ConnectionId connection, ConnectionCloseReason reason, std::string detail, bool abortive) {
		auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return;

		auto& state = iterator->second;
		state.CloseMode = ConnectionCloseMode::Immediate;
		state.ReceivePaused = true;

		/*
			Destroying queued outbound buffers releases any unsent
			resource reservations before the connection is retired.
		*/
		state.OutboundQueue.clear();

		if (state.Socket) {
			[[maybe_unused]]
			const bool removed = RemoveEpollInterest(m_Epoll.Get(), state.Socket.Get());
			if (abortive)
				SetAbortiveClose(state.Socket.Get());

			state.Socket.Reset();
		}

		if (m_Resources != nullptr)
			m_Resources->DeactivateConnection(connection);

		QueuePushResult event_result{ QueuePushResult::Queued };

		if (!state.CloseEventEmitted) {
			state.CloseEventEmitted = true;

			event_result = PushEvent(ConnectionClosedEvent{
					.Connection = connection,
					.Reason = reason,
					.Detail = std::move(detail),
				});
		}

		m_Connections.erase(iterator);

		if (event_result != QueuePushResult::Queued && !m_ShuttingDown)
			BeginShutdown();
	}

	void LinuxNetworkBackend::BeginShutdown() {
		if (m_ShuttingDown) return;

		m_ShuttingDown = true;
		m_StopRequested.store(true, std::memory_order_release);
		m_Listener.Reset();

		while (!m_Connections.empty()) {
			const ConnectionId connection = m_Connections.begin()->first;
			CloseConnection(connection, ConnectionCloseReason::ServerStopping, "Network backend is shutting down", true);
		}
	}

	QueuePushResult LinuxNetworkBackend::PushEvent(NetworkEvent event, NetworkResourceLedger::Reservation reservation) {
		if (m_Events == nullptr)
			return QueuePushResult::Closed;

		return m_Events->Push(QueuedNetworkEvent{
				.Event = std::move(event),
				.InboundReservation = std::move(reservation),
			});
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
			[[maybe_unused]]
			const auto result = PushEvent(
				NetworkFailureEvent{
					.Error = error,
					.Message = std::move(message),
					.Fatal = fatal,
				});
		}
		catch (...) {}
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
		}
		catch (...) {
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
		}
		catch (...) {
			m_StartupCondition.notify_all();
		}
	}
}
