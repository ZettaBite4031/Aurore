
#include "WindowsBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace Aurore::Network::Detail::Windows {
	namespace {
		constexpr DWORD WorkerPollIntervalMilliseconds{ 250 };

		[[nodiscard]] HANDLE SocketAsHandle(SOCKET socket) noexcept {
			return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(socket));
		}

		[[nodiscard]] ULONG_PTR ToCompletionKey(CompletionKey key) noexcept {
			return static_cast<ULONG_PTR>(key);
		}
	}

	WindowsNetworkBackend::~WindowsNetworkBackend() noexcept {
		Shutdown();
	}

	NetworkResult<void> WindowsNetworkBackend::Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) {
		if (m_Initialized) return std::unexpected(NetworkError::AlreadyInitialized);
		if (config.ReceiveBufferSize == 0 || config.ReceiveBufferSize > std::numeric_limits<ULONG>::max()) return std::unexpected(NetworkError::InvalidConfiguration);

		auto winsock_result = m_Winsock.Initialize();
		if (!winsock_result) return std::unexpected(winsock_result.error());

		m_CompletionPort.Reset(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
		if (!m_CompletionPort) {
			m_Winsock.Reset();
			return std::unexpected(NetworkError::BackendFailure);
		}

		m_Config = config;
		m_Commands = &commands;
		m_Events = &events;
		m_Resources = &resources;
		m_NextConnectionId = 1;
		m_Initialized = true;
		return {};
	}

	NetworkResult<NetworkEndpoint> WindowsNetworkBackend::Start() {
		if (!m_Initialized)
			return std::unexpected(NetworkError::NotInitialized);

		if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable())
			return std::unexpected(NetworkError::AlreadyRunning);

		if (!m_CompletionPort) {
			m_CompletionPort.Reset(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
			if (!m_CompletionPort)
				return std::unexpected(NetworkError::BackendFailure);
		}

		auto listener_res = CreateListener();
		if (!listener_res)
			return std::unexpected(listener_res.error());

		m_BoundEndpoint = *listener_res;
		m_StopRequested.store(false, std::memory_order_release);
		m_ShuttingDown = false;
		m_WorkerFailureReported = false;

		{
			std::scoped_lock lock(m_StartupMutex);
			m_StartupComplete = false;
			m_StartupError.reset();
		}

		m_Started.store(true, std::memory_order_release);

		try {
			m_Worker = std::thread(&WindowsNetworkBackend::WorkerMain, this);
		}
		catch (...) {
			m_Started.store(false, std::memory_order_release);
			m_Listener.Reset();
			m_BoundEndpoint.reset();

			return std::unexpected(NetworkError::BackendFailure);
		}

		if (!PostQueuedCompletionStatus(m_CompletionPort.Get(), 0, ToCompletionKey(CompletionKey::Startup), nullptr))
			m_StopRequested.store(true, std::memory_order_release);

		std::optional<NetworkError> startup_error;
#if defined(_DEBUG)
		bool startup_timed_out{ false };
#endif
		{
			std::unique_lock lock(m_StartupMutex);
#if defined(_DEBUG)
			constexpr auto startup_timeout = std::chrono::seconds{ 5 };
			const bool completed = m_StartupCondition.wait_for(lock, startup_timeout, [this] { return m_StartupComplete; });
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
#if defined(_DEBUG)
		if (startup_timed_out) {
			if (m_CompletionPort) [[maybe_unused]] const BOOL posted = PostQueuedCompletionStatus(m_CompletionPort.Get(), 0, ToCompletionKey(CompletionKey::Stop), nullptr);
			m_StopRequested.store(true, std::memory_order_release);
		}
#endif

		if (startup_error.has_value()) {
			if (m_Worker.joinable()) m_Worker.join();

			m_Started.store(false, std::memory_order_release);
			m_Listener.Reset();
			m_BoundEndpoint.reset();

			return std::unexpected(*startup_error);
		}

		return *m_BoundEndpoint;
	}

	void WindowsNetworkBackend::NotifyCommandAvailable() noexcept {
		if (!m_Started.load(std::memory_order_acquire) || !m_CompletionPort) return;
		if (!PostQueuedCompletionStatus(m_CompletionPort.Get(), 0, ToCompletionKey(CompletionKey::Command), nullptr))
			m_StopRequested.store(true, std::memory_order_release);
	}

	void WindowsNetworkBackend::Stop() noexcept {
		if (!m_Worker.joinable()) {
			m_Started.store(false, std::memory_order_release);
			m_Listener.Reset();
			m_BoundEndpoint.reset();
			return;
		}

		/*
			Post the wake-up before publishing the stop flag.

			This prevents the worker from observing the flag and exiting
			between the flag store and the completion packet being posted.
		*/
		if (m_CompletionPort) {
			[[maybe_unused]]
			const BOOL posted = PostQueuedCompletionStatus(m_CompletionPort.Get(), 0, ToCompletionKey(CompletionKey::Stop), nullptr);
		}

		m_StopRequested.store(true, std::memory_order_release);

		m_Worker.join();

		m_Started.store(false, std::memory_order_release);
		m_Listener.Reset();
		m_BoundEndpoint.reset();
	}

	void WindowsNetworkBackend::Shutdown() noexcept {
		Stop();

		if (m_Resources != nullptr) {
			for (const auto& [connection_id, connection] : m_Connections) {
				(void)connection;
				m_Resources->DeactivateConnection(connection_id);
			}
		}

		m_Connections.clear();
		m_Operations.clear();

		m_AcceptEx = nullptr;
		m_GetAcceptExSockaddrs = nullptr;
		m_ListenerFamily = AF_UNSPEC;
		m_CompletionPort.Reset();
		m_Winsock.Reset();

		m_Commands = nullptr;
		m_Events = nullptr;
		m_Resources = nullptr;
		m_Config = {};
		m_Initialized = false;
		m_ShuttingDown = false;
		m_StopRequested.store(false, std::memory_order_release);
	}

	NetworkResult<NetworkEndpoint> WindowsNetworkBackend::CreateListener() {
		m_Listener.Reset();
		m_AcceptEx = nullptr;
		m_GetAcceptExSockaddrs = nullptr;
		m_ListenerFamily = AF_UNSPEC;

		ADDRINFOA hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		const std::string service = std::to_string(m_Config.Port);
		const char* node = m_Config.BindAddress == "*" ? nullptr : m_Config.BindAddress.c_str();

		ADDRINFOA* rawAddressInfo = nullptr;
		const int resolve_res = getaddrinfo(node, service.c_str(), &hints, &rawAddressInfo);
		if (resolve_res != 0) {
			EmitFailure(NetworkError::BackendFailure, "Failed to resolve network bind address: " + FormatSocketError(resolve_res), true);
			return std::unexpected(NetworkError::BackendFailure);
		}

		UniqueAddressInfo addresses(rawAddressInfo);

		for (ADDRINFOA* addr = addresses.get(); addr != nullptr; addr = addr->ai_next) {
			UniqueSocket candidate(WSASocketA(addr->ai_family, addr->ai_socktype, addr->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED));
			if (!candidate) continue;
			if (!SetExclusiveAddressUse(candidate.Get())) continue;
			if (bind(candidate.Get(), addr->ai_addr, static_cast<int>(addr->ai_addrlen)) == SOCKET_ERROR) continue;
			if (listen(candidate.Get(), SOMAXCONN) == SOCKET_ERROR) continue;
			if (!AssociateSocket(candidate.Get())) continue;

			m_ListenerFamily = addr->ai_family;
			m_Listener = std::move(candidate);

			if (!LoadExtensionFunctions()) {
				m_Listener.Reset();
				m_ListenerFamily = AF_UNSPEC;
				continue;
			}

			sockaddr_storage local_addr{};
			int local_addr_len = static_cast<int>(sizeof(local_addr));

			if (getsockname(m_Listener.Get(), reinterpret_cast<sockaddr*>(&local_addr), &local_addr_len) == SOCKET_ERROR) {
				m_Listener.Reset();
				m_ListenerFamily = AF_UNSPEC;
				continue;
			}

			auto endpoint = MakeNetworkEndpoint(reinterpret_cast<sockaddr*>(&local_addr), local_addr_len);
			if (!endpoint.has_value()) {
				m_Listener.Reset();
				m_ListenerFamily = AF_UNSPEC;
				continue;
			}

			return *endpoint;
		}

		EmitFailure(NetworkError::BackendFailure, "Unable to bind and listen on " + m_Config.BindAddress + ":" + std::to_string(m_Config.Port), true);
		return std::unexpected(NetworkError::BackendFailure);
	}

	bool WindowsNetworkBackend::LoadExtensionFunctions() noexcept {
		GUID acceptExGuid = WSAID_ACCEPTEX;
		DWORD bytes_returned = 0;

		if (WSAIoctl(m_Listener.Get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptExGuid,
			static_cast<DWORD>(sizeof(acceptExGuid)), &m_AcceptEx, static_cast<DWORD>(sizeof(m_AcceptEx)),
			&bytes_returned, nullptr, nullptr) == SOCKET_ERROR) {
			m_AcceptEx = nullptr;
			return false;
		}

		GUID addrGuid = WSAID_GETACCEPTEXSOCKADDRS;
		bytes_returned = 0;

		if (WSAIoctl(m_Listener.Get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &addrGuid,
			static_cast<DWORD>(sizeof(addrGuid)), &m_GetAcceptExSockaddrs, static_cast<DWORD>(sizeof(m_GetAcceptExSockaddrs)),
			&bytes_returned, nullptr, nullptr) == SOCKET_ERROR) {
			m_GetAcceptExSockaddrs = nullptr;
			return false;
		}

		return true;
	}

	bool WindowsNetworkBackend::AssociateSocket(SOCKET socket) noexcept {
		if (!m_CompletionPort || socket == INVALID_SOCKET) return false;
		const HANDLE result = CreateIoCompletionPort(SocketAsHandle(socket), m_CompletionPort.Get(), ToCompletionKey(CompletionKey::Socket), 0);
		return result == m_CompletionPort.Get();
	}

	void WindowsNetworkBackend::WorkerMain() noexcept {
		while (true) {
			try {
				if (m_StopRequested.load(std::memory_order_acquire) && !m_ShuttingDown) {
					if (!IsStartupComplete()) SignalStartup(NetworkError::BackendFailure);
					BeginShutdown();
				}

				const DWORD timeout = m_ShuttingDown && m_Operations.empty() ? 0 : WorkerPollIntervalMilliseconds;

				DWORD transferred_bytes{ 0 };
				ULONG_PTR completion_key{ 0 };
				OVERLAPPED* overlapped{ nullptr };

				const BOOL succeeded = GetQueuedCompletionStatus(m_CompletionPort.Get(), &transferred_bytes, &completion_key, &overlapped, timeout);
				const WorkerCompletion completion{
					.Succeeded = succeeded != FALSE,
					.TransferredBytes = transferred_bytes,
					.CompletionKey = completion_key,
					.Overlapped = overlapped,
					.Error = succeeded ? ERROR_SUCCESS : GetLastError()
				};
				if (ProcessCompletion(completion) == WorkerStepResult::Exit) break;
			}
			catch (const std::exception& ex) {
				HandleWorkerException(ex.what());
			}
			catch (...) {
				HandleWorkerException("Unhandled non-standard exception in the IOCP worker");
			}
		}

		SignalStartupFailureNoexcept();
		m_Started.store(false, std::memory_order_release);
	}

	WindowsNetworkBackend::WorkerStepResult WindowsNetworkBackend::ProcessCompletion(const WorkerCompletion& completion) {
		/*
			A null OVERLAPPED pointer represents either a control
			completion or a failure of the completion-port wait itself.
		*/
		if (completion.Overlapped == nullptr) {
			if (!completion.Succeeded) {
				if (completion.Error == WAIT_TIMEOUT) {
					if (m_ShuttingDown && m_Operations.empty()) return WorkerStepResult::Exit;
					return WorkerStepResult::Continue;
				}
				if (completion.Error == ERROR_ABANDONED_WAIT_0) {
					SignalStartupFailureNoexcept();
					return WorkerStepResult::Exit;
				}
				EmitFailure(NetworkError::BackendFailure, "GetQueuedCompletionStatus failed: " + FormatWindowsError(completion.Error), true);
				SignalStartupFailureNoexcept();
				BeginShutdown();
				return WorkerStepResult::Continue;
			}
			HandleControlCompletion(static_cast<CompletionKey>(completion.CompletionKey));
			return WorkerStepResult::Continue;
		}

		/*
			A non-null OVERLAPPED pointer represents a completed
			submitted operation, including failed and canceled I/O.
		*/
		auto operation = TakeOperation(completion.Overlapped);
		if (!operation) {
			EmitFailure(NetworkError::BackendFailure, "Received an IOCP completion for an unknown operation", true);
			BeginShutdown();
			return WorkerStepResult::Continue;
		}

		HandleIoCompletion(std::move(operation), completion.TransferredBytes, completion.Error);
		return WorkerStepResult::Continue;
	}

	void WindowsNetworkBackend::HandleControlCompletion(CompletionKey key) {
		switch (key) {
		case CompletionKey::Command: DrainCommands(); break;
		case CompletionKey::Startup: HandleStartup(); break;
		case CompletionKey::Stop: BeginShutdown(); break;
		case CompletionKey::Socket:
		default:
			EmitFailure(NetworkError::BackendFailure, "Received an invalid IOCP control completion", true);
			BeginShutdown();
			break;
		}
	}

	void WindowsNetworkBackend::HandleIoCompletion(std::unique_ptr<IoOperation> operation, DWORD transferred_bytes, DWORD error) {
		switch (operation->Kind) {
		case IoOperationKind::Accept: {
			auto* raw = static_cast<AcceptOperation*>(operation.release());
			HandleAcceptCompletion(std::unique_ptr<AcceptOperation>(raw), transferred_bytes, error);
		} break;
		case IoOperationKind::Receive: {
			auto* raw = static_cast<ReceiveOperation*>(operation.release());
			HandleReceiveCompletion(std::unique_ptr<ReceiveOperation>(raw), transferred_bytes, error);
		} break;
		case IoOperationKind::Send: {
			auto* raw = static_cast<SendOperation*>(operation.release());
			HandleSendCompletion(std::unique_ptr<SendOperation>(raw), transferred_bytes, error);
		} break;
		}
	}

	void WindowsNetworkBackend::HandleAcceptCompletion(std::unique_ptr<AcceptOperation> operation, DWORD transferred_bytes, DWORD error) {
		(void)transferred_bytes;
		if (m_ShuttingDown) return;

		if (error != ERROR_SUCCESS) {
			if (error != WSAECONNRESET && error != WSA_OPERATION_ABORTED) EmitFailure(NetworkError::BackendFailure, "AcceptEx completion failure: " + FormatSocketError(static_cast<int>(error)), true);
			if (!PostAccept()) {
				EmitFailure(NetworkError::BackendFailure, "Failed to replenish the AcceptEx pipeline", true);
				BeginShutdown();
			}
			return;
		}

		if (!operation->AcceptedSocket) {
			EmitFailure(NetworkError::BackendFailure, "AcceptEx completed without an accepted socket", true);
			BeginShutdown();
			return;
		}

		const SOCKET listener_socket = m_Listener.Get();
		if (setsockopt(operation->AcceptedSocket.Get(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char*>(&listener_socket), static_cast<int>(sizeof(listener_socket))) == SOCKET_ERROR) {
			EmitFailure(NetworkError::BackendFailure, "Failed to update accepted socket context: " + FormatSocketError(WSAGetLastError()), false);
			if (!PostAccept()) BeginShutdown();
			return;
		}
		if (!SetTcpNoDelay(operation->AcceptedSocket.Get())) {
			EmitFailure(NetworkError::BackendFailure, "Failed to enable TCP_NODELAY on an accepted socket: " + FormatSocketError(WSAGetLastError()), false);
			if (!PostAccept()) BeginShutdown();
			return;
		}
		if (!AssociateSocket(operation->AcceptedSocket.Get())) {
			EmitFailure(NetworkError::BackendFailure, "Failed to associate an accepted socket with IOCP: " + FormatWindowsError(GetLastError()), false);
			if (!PostAccept()) BeginShutdown();
			return;
		}

		sockaddr* local_address{ nullptr };
		sockaddr* remote_address{ nullptr };
		int local_address_length{ 0 };
		int remote_address_length{ 0 };
		m_GetAcceptExSockaddrs(operation->AddressBuffer.data(), AcceptReceiveDataLength, AcceptAddressLength, AcceptAddressLength, &local_address, &local_address_length, &remote_address, &remote_address_length);

		auto local_endpoint = MakeNetworkEndpoint(local_address, local_address_length);
		auto remote_endpoint = MakeNetworkEndpoint(remote_address, remote_address_length);
		if (!local_endpoint || !remote_endpoint) {
			EmitFailure(NetworkError::BackendFailure, "Failed to decode accepted socket endpoints", false);
			if (!PostAccept()) BeginShutdown();
			return;
		}

		if (m_Connections.size() >= m_Config.MaximumConnections) {
			SetAbortiveClose(operation->AcceptedSocket.Get());
			operation->AcceptedSocket.Reset();
			if (!PostAccept()) BeginShutdown();
			return;
		}

		const ConnectionId connection_id = AllocateConnectionId();
		auto [iterator, inserted] = m_Connections.try_emplace(connection_id, connection_id, std::move(operation->AcceptedSocket), *local_endpoint, *remote_endpoint);
		if (!inserted) {
			EmitFailure(NetworkError::BackendFailure, "Failed to register newly accepted connection", true);
			BeginShutdown();
			return;
		}

		if (m_Resources == nullptr || !m_Resources->RegisterConnection(connection_id)) {
			SetAbortiveClose(iterator->second.Socket.Get());
			iterator->second.Socket.Reset();
			m_Connections.erase(iterator);
			EmitFailure(NetworkError::BackendFailure, "Failed to register connection resource accounting", true);
			BeginShutdown();
			return;
		}

		const auto event_result = PushEvent(ConnectionOpenedEvent{ .Connection = connection_id, .LocalEndpoint = *local_endpoint, .RemoteEndpoint = *remote_endpoint });
		if (event_result != QueuePushResult::Queued) {
			CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "Network event queue could not accept a connection-opened event", true);
			BeginShutdown();
			return;
		}

		[[maybe_unused]] const bool receive_posted = PostReceive(connection_id);
		if (!m_ShuttingDown && !PostAccept()) {
			EmitFailure(NetworkError::BackendFailure, "Failed to replenish the AcceptEx pipeline", true);
			BeginShutdown();
		}
	}

	void WindowsNetworkBackend::HandleReceiveCompletion(std::unique_ptr<ReceiveOperation> operation, DWORD transferred_bytes, DWORD error) {
		const ConnectionId connection_id = operation->Connection;
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) {
			EmitFailure(NetworkError::BackendFailure, "Receive completion referenced an unknown connection", true);
			BeginShutdown();
			return;
		}

		auto& connection = iterator->second;
		if (connection.PendingOperationCount == 0) {
			EmitFailure(NetworkError::BackendFailure, "Receive completion underflowed pending-operation accounting", true);
			BeginShutdown();
			return;
		}

		connection.PendingOperationCount--;
		connection.ReceivePending = false;
		connection.ReceiveOverlapped = nullptr;

		if (connection.SocketClosed) {
			TryRetireConnection(connection_id);
			return;
		}

		if (error != ERROR_SUCCESS) {
			if (error == ERROR_OPERATION_ABORTED && connection.CloseMode == ConnectionCloseMode::AfterFlush) {
				connection.ReceivePaused = true;
				TryCompleteCloseAfterFlush(connection_id);
				return;
			}
			CloseConnection(connection_id, ConnectionCloseReason::TransportError, "WSARecv failed: " + FormatSocketError(static_cast<int>(error)), true);
			return;
		}

		if (transferred_bytes == 0) {
			if (connection.CloseMode == ConnectionCloseMode::AfterFlush) {
				connection.ReceivePaused = true;
				TryCompleteCloseAfterFlush(connection_id);
				return;
			}
			CloseConnection(connection_id, ConnectionCloseReason::RemoteClosed, "Remote peer closed the connection", false);
			return;
		}

		if (connection.CloseMode != ConnectionCloseMode::Open) {
			connection.ReceivePaused = true;
			TryCompleteCloseAfterFlush(connection_id);
			return;
		}

		operation->Buffer.resize(transferred_bytes);
		connection.ReceivePaused = true;
		if (m_Resources == nullptr) {
			CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "Network resource ledger is unavailable", true);
			BeginShutdown();
			return;
		}

		auto reservation = m_Resources->ReserveInboundEvent(connection_id, operation->Buffer.size());
		if (!reservation) {
			if (reservation.error() == NetworkResourceLedger::ReserveError::InvalidConnection) {
				CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "Inbound accounting referenced an inactive connection", true);
				BeginShutdown();
			}
			else CloseConnection(connection_id, ConnectionCloseReason::InboundLimitExceeded, "Inbound event memory limit exceeded", true);
			return;
		}

		const auto event_result = PushEvent(BytesReceivedEvent{ .Connection = connection_id, .Data = std::move(operation->Buffer) }, std::move(*reservation));
		if (event_result != QueuePushResult::Queued) {
			CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, event_result == QueuePushResult::Closed ? "Network event queue was closed" : "Network event queue budget was exceeded", true);
			BeginShutdown();
		}
	}

	void WindowsNetworkBackend::HandleSendCompletion(std::unique_ptr<SendOperation> operation, DWORD transferred_bytes, DWORD error) {
		const ConnectionId connection_id = operation->Connection;
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) {
			EmitFailure(NetworkError::BackendFailure, "Send completion referenced an unknown connection", true);
			BeginShutdown();
			return;
		}

		auto& connection = iterator->second;
		if (connection.PendingOperationCount == 0) {
			EmitFailure(NetworkError::BackendFailure, "Send completion underflowed pending-operation accounting", true);
			BeginShutdown();
			return;
		}

		connection.PendingOperationCount--;
		connection.SendPending = false;
		if (connection.SocketClosed) {
			TryRetireConnection(connection_id);
			return;
		}
		if (error != ERROR_SUCCESS) {
			CloseConnection(connection_id, ConnectionCloseReason::TransportError, "WSASend failed: " + FormatSocketError(static_cast<int>(error)), true);
			return;
		}

		const std::size_t remaining = operation->Remaining();
		if (transferred_bytes == 0 || transferred_bytes > remaining) {
			CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "WSASend returned an invalid transfer length", true);
			return;
		}

		const std::size_t transferred = static_cast<std::size_t>(transferred_bytes);
		operation->Offset += transferred;
		operation->OutboundReservation.Release(transferred);

		if (operation->Remaining() != 0) {
			if (SubmitSendOperation(connection_id, std::move(operation))) return;
			auto current = m_Connections.find(connection_id);
			if (current != m_Connections.end() && !current->second.SocketClosed) CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "Failed to continue a partial send", true);
			return;
		}

		StartNextSend(connection_id);
	}

	void WindowsNetworkBackend::HandleWorkerException(std::string_view msg) noexcept {
		m_StopRequested.store(true, std::memory_order_release);
		SignalStartupFailureNoexcept();

		if (!m_WorkerFailureReported) {
			m_WorkerFailureReported = true;

			/*
				Reporting is best-effort because event construction and
				queue insertion may themselves allocate.
			*/
			try {
				std::string detail{
					"Unhandled exception in IOCP worker"
				};

				if (!msg.empty()) {
					detail += ": ";
					detail.append(msg);
				}

				EmitFailure(
					NetworkError::BackendFailure,
					std::move(detail),
					true);
			}
			catch (...) {
				/*
					Exception containment must not fail while trying
					to report the original exception.
				*/
			}
		}

		BeginEmergencyShutdown();
	}

	void WindowsNetworkBackend::BeginEmergencyShutdown() noexcept {
		m_ShuttingDown = true;
		m_Listener.Reset();

		for (auto& [overlapped, operation] : m_Operations) {
			(void)overlapped;
			if (!operation || operation->Kind != IoOperationKind::Accept) continue;
			auto* accept_operation = static_cast<AcceptOperation*>(operation.get());
			accept_operation->AcceptedSocket.Reset();
		}

		for (auto iterator = m_Connections.begin(); iterator != m_Connections.end();) {
			auto& connection = iterator->second;
			connection.CloseMode = ConnectionCloseMode::Immediate;
			connection.RequestedCloseReason = ConnectionCloseReason::BackendFailure;
			connection.ReceivePaused = true;
			connection.OutboundQueue.clear();
			if (m_Resources != nullptr) m_Resources->DeactivateConnection(iterator->first);

			if (!connection.SocketClosed) {
				SetAbortiveClose(connection.Socket.Get());
				connection.Socket.Reset();
				connection.SocketClosed = true;
			}

			if (connection.PendingOperationCount == 0) iterator = m_Connections.erase(iterator);
			else ++iterator;
		}
	}

	void WindowsNetworkBackend::SignalStartupFailureNoexcept() noexcept {
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
			/*
				Standard mutex acquisition should not normally fail,
				but nothing may escape the worker entry point.
			*/
		}
	}

	void WindowsNetworkBackend::HandleStartup() {
		if (m_ShuttingDown || m_StopRequested.load(std::memory_order_acquire)) {
			SignalStartup(NetworkError::BackendFailure);
			BeginShutdown();
			return;
		}

		if (!PostInitialAccepts()) {
			EmitFailure(NetworkError::BackendFailure, "Failed to initialize the AcceptEx pipeline", true);
			SignalStartup(NetworkError::BackendFailure);
			BeginShutdown();
			return;
		}

		SignalStartup(std::nullopt);
	}

	void WindowsNetworkBackend::DrainCommands() {
		if (!m_Commands) return;

		while (auto command = m_Commands->TryPop()) {
			std::visit([this](auto&& value) { HandleCommand(std::forward<decltype(value)>(value)); }, std::move(*command));
		}
	}

	void WindowsNetworkBackend::HandleCommand(SendCommand&& command) {
		if (m_ShuttingDown) return;
		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) {
			EmitFailure(NetworkError::InvalidConnectionId, "A send command referenced an unknown connection", false);
			return;
		}

		auto& connection = iterator->second;
		if (connection.SocketClosed || connection.CloseMode != ConnectionCloseMode::Open) return;
		connection.OutboundQueue.push_back(OutboundBuffer{ .Data = std::move(command.Data), .OutboundReservation = std::move(command.OutboundReservation) });
		StartNextSend(command.Connection);
	}

	void WindowsNetworkBackend::HandleCommand(const CloseAfterFlushCommand& command) {
		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) return;

		auto& connection = iterator->second;
		if (connection.SocketClosed) return;
		connection.CloseMode = ConnectionCloseMode::AfterFlush;
		connection.RequestedCloseReason = command.Reason;
		connection.ReceivePaused = true;

		if (!CancelPendingReceiveForClose(command.Connection)) {
			CloseConnection(command.Connection, ConnectionCloseReason::BackendFailure, "Failed to cancel the pending receive for close-after-flush", true);
			return;
		}
		TryCompleteCloseAfterFlush(command.Connection);
	}

	void WindowsNetworkBackend::HandleCommand(const CloseImmediatelyCommand& command) {
		CloseConnection(command.Connection, command.Reason, "Connection closed immediately by application", true);
	}

	void WindowsNetworkBackend::HandleCommand(const ResumeReceiveCommand& command) {
		if (m_ShuttingDown) return;

		auto iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end()) return;

		auto& connection = iterator->second;
		if (connection.SocketClosed || connection.CloseMode != ConnectionCloseMode::Open || connection.ReceivePending || !connection.ReceivePaused) return;

		connection.ReceivePaused = false;

		if (PostReceive(command.Connection)) return;

		iterator = m_Connections.find(command.Connection);
		if (iterator == m_Connections.end() || iterator->second.SocketClosed || m_ShuttingDown) return;
		CloseConnection(command.Connection, ConnectionCloseReason::BackendFailure, "Failed to resume receiving data", true);
	}

	void WindowsNetworkBackend::HandleCommand(const StopCommand& command) {
		(void)command;
		BeginShutdown();
	}

	bool WindowsNetworkBackend::PostInitialAccepts() {
		const std::size_t accept_depth = std::min(DefaultAcceptDepth, std::max<std::size_t>(1, m_Config.MaximumConnections));
		for (std::size_t index{ 0 }; index < accept_depth; index++)
			if (!PostAccept()) return false;
		return true;
	}

	bool WindowsNetworkBackend::PostAccept() {
		if (m_ShuttingDown || !m_Listener || m_AcceptEx == nullptr) return false;

		auto operation = std::make_unique<AcceptOperation>();
		operation->AcceptedSocket.Reset(WSASocketA(m_ListenerFamily, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
		if (!operation->AcceptedSocket) return false;

		OVERLAPPED* overlapped = &operation->Overlapped;
		auto* rawOperation = operation.get();

		const auto [iterator, inserted] = m_Operations.emplace(overlapped, std::move(operation));
		if (!inserted) return false;

		DWORD bytes_received = 0;

		const BOOL accepted = m_AcceptEx(m_Listener.Get(), rawOperation->AcceptedSocket.Get(), rawOperation->AddressBuffer.data(),
			AcceptReceiveDataLength, AcceptAddressLength, AcceptAddressLength, &bytes_received, overlapped);
		if (!accepted) {
			const int error = WSAGetLastError();
			if (error != ERROR_IO_PENDING) {
				m_Operations.erase(iterator);
				return false;
			}
		}

		return true;
	}

	bool WindowsNetworkBackend::PostReceive(ConnectionId connection_id) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return false;
		auto& connection = iterator->second;
		if (m_ShuttingDown || connection.SocketClosed || connection.CloseMode != ConnectionCloseMode::Open || connection.ReceivePending || connection.ReceivePaused) return false;

		auto operation = std::make_unique<ReceiveOperation>(connection_id, m_Config.ReceiveBufferSize);
		OVERLAPPED* overlapped = &operation->Overlapped;
		auto* raw_operation = operation.get();
		const auto [operation_iterator, inserted] = m_Operations.emplace(overlapped, std::move(operation));
		if (!inserted) return false;

		connection.PendingOperationCount++;
		connection.ReceivePending = true;
		connection.ReceiveOverlapped = overlapped;

		const int result = WSARecv(connection.Socket.Get(), &raw_operation->BufferDescriptor, 1, nullptr, &raw_operation->Flags, overlapped, nullptr);
		if (result == SOCKET_ERROR) {
			const int socket_error = WSAGetLastError();
			if (socket_error != WSA_IO_PENDING) {
				m_Operations.erase(operation_iterator);
				connection.PendingOperationCount--;
				connection.ReceivePending = false;
				connection.ReceiveOverlapped = nullptr;
				CloseConnection(connection_id, ConnectionCloseReason::TransportError, "Unable to post WSARecv: " + FormatSocketError(socket_error), true);
				return false;
			}
		}
		return true;
	}

	void WindowsNetworkBackend::StartNextSend(ConnectionId connection_id) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return;
		auto& connection = iterator->second;
		if (connection.SocketClosed || connection.SendPending) return;

		if (connection.OutboundQueue.empty()) {
			TryCompleteCloseAfterFlush(connection_id);
			return;
		}

		auto outbound = std::move(connection.OutboundQueue.front());
		connection.OutboundQueue.pop_front();
		auto operation = std::make_unique<SendOperation>(connection_id, std::move(outbound));
		if (SubmitSendOperation(connection_id, std::move(operation))) return;

		iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end() || iterator->second.SocketClosed) return;
		CloseConnection(connection_id, ConnectionCloseReason::BackendFailure, "Failed to submit an outbound send operation", true);
	}


	void WindowsNetworkBackend::TryCompleteCloseAfterFlush(ConnectionId connection_id) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return;
		auto& connection = iterator->second;
		if (connection.SocketClosed || connection.CloseMode != ConnectionCloseMode::AfterFlush) return;
		if (connection.SendPending || !connection.OutboundQueue.empty() || connection.ReceivePending) return;
		CloseConnection(connection_id, connection.RequestedCloseReason, "Connection closed after flushing outbound data", false);
	}

	bool WindowsNetworkBackend::CancelPendingReceiveForClose(ConnectionId connection_id) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return false;
		auto& connection = iterator->second;
		if (!connection.ReceivePending) return true;
		if (connection.ReceiveOverlapped == nullptr) {
			EmitFailure(NetworkError::BackendFailure, "Connection reported a pending receive without a tracked OVERLAPPED operation", true);
			BeginShutdown();
			return false;
		}

		const BOOL canceled = CancelIoEx(SocketAsHandle(connection.Socket.Get()), connection.ReceiveOverlapped);
		if (canceled) return true;
		const DWORD error = GetLastError();
		if (error == ERROR_NOT_FOUND) return true;
		EmitFailure(NetworkError::BackendFailure, "Failed to cancel pending receive during close-after-flush: " + FormatWindowsError(error), false);
		return false;
	}

	bool WindowsNetworkBackend::SubmitSendOperation(ConnectionId connection_id, std::unique_ptr<SendOperation> operation) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end() || operation == nullptr) return false;

		auto& connection = iterator->second;
		if (connection.SocketClosed || connection.SendPending || operation->Remaining() == 0) return false;

		operation->Overlapped = {};
		operation->RefreshBuffer();

		OVERLAPPED* overlapped = &operation->Overlapped;
		auto* raw_operation = operation.get();

		const auto [operation_iterator, inserted] = m_Operations.emplace(overlapped, std::move(operation));
		if (!inserted) return false;

		connection.PendingOperationCount++;
		connection.SendPending = true;

		const int result = WSASend(connection.Socket.Get(), &raw_operation->BufferDescriptor, 1, nullptr, 0, overlapped, nullptr);
		if (result == SOCKET_ERROR) {
			const int error = WSAGetLastError();
			if (error != WSA_IO_PENDING) {
				m_Operations.erase(operation_iterator);
				connection.PendingOperationCount--;
				connection.SendPending = false;

				CloseConnection(connection_id, ConnectionCloseReason::TransportError, "Unable to post WSASend: " + FormatSocketError(error), true);
				return false;
			}
		}

		return true;
	}

	std::unique_ptr<IoOperation> WindowsNetworkBackend::TakeOperation(OVERLAPPED* overlapped) {
		const auto iterator = m_Operations.find(overlapped);
		if (iterator == m_Operations.end()) return nullptr;
		auto operation = std::move(iterator->second);
		m_Operations.erase(iterator);
		return operation;
	}

	ConnectionId WindowsNetworkBackend::AllocateConnectionId() noexcept {
		while (true) {
			ConnectionId candidate{ .Value = m_NextConnectionId++ };
			if (m_NextConnectionId == 0) m_NextConnectionId = 1;
			if (candidate && !m_Connections.contains(candidate)) return candidate;
		}
	}

	void WindowsNetworkBackend::CloseConnection(ConnectionId connection_id, ConnectionCloseReason reason, std::string detail, bool abortive) {
		auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return;
		auto& connection = iterator->second;

		if (!connection.SocketClosed) {
			connection.CloseMode = ConnectionCloseMode::Immediate;
			connection.ReceivePaused = true;
			connection.OutboundQueue.clear();
			if (m_Resources != nullptr) m_Resources->DeactivateConnection(connection_id);
			if (abortive) SetAbortiveClose(connection.Socket.Get());
			connection.Socket.Reset();
			connection.SocketClosed = true;
		}

		QueuePushResult event_result{ QueuePushResult::Queued };
		if (!connection.CloseEventEmitted) {
			connection.CloseEventEmitted = true;
			event_result = PushEvent(ConnectionClosedEvent{ .Connection = connection_id, .Reason = reason, .Detail = std::move(detail) });
		}

		TryRetireConnection(connection_id);
		if (event_result != QueuePushResult::Queued && !m_ShuttingDown) BeginShutdown();
	}

	void WindowsNetworkBackend::TryRetireConnection(ConnectionId connection_id) {
		const auto iterator = m_Connections.find(connection_id);
		if (iterator == m_Connections.end()) return;
		if (iterator->second.SocketClosed && iterator->second.PendingOperationCount == 0)
			m_Connections.erase(iterator);
	}

	void WindowsNetworkBackend::BeginShutdown() {
		if (m_ShuttingDown) return;

		m_ShuttingDown = true;
		m_Listener.Reset();

		for (auto& [overlapped, operation] : m_Operations) {
			(void)overlapped;
			if (operation->Kind == IoOperationKind::Accept) {
				auto* acceptOperation = static_cast<AcceptOperation*>(operation.get());
				acceptOperation->AcceptedSocket.Reset();
			}
		}

		std::vector<ConnectionId> connections;
		connections.reserve(m_Connections.size());

		for (const auto& [connection_id, connection] : m_Connections) {
			(void)connection;
			connections.push_back(connection_id);
		}

		for (const ConnectionId connection_id : connections) {
			CloseConnection(connection_id, ConnectionCloseReason::ServerStopping, "Network backend is shutting down", true);
		}
	}

	QueuePushResult WindowsNetworkBackend::PushEvent(NetworkEvent event, NetworkResourceLedger::Reservation reservation) {
		if (m_Events == nullptr) return QueuePushResult::Closed;
		return m_Events->Push(QueuedNetworkEvent{ .Event = std::move(event), .InboundReservation = std::move(reservation) });
	}

	void WindowsNetworkBackend::EmitFailure(NetworkError error, std::string message, bool fatal) {
		[[maybe_unused]] const auto result = PushEvent(NetworkFailureEvent{ .Error = error, .Message = std::move(message), .Fatal = fatal });
	}

	void WindowsNetworkBackend::SignalStartup(std::optional<NetworkError> error) {
		{
			std::scoped_lock lock(m_StartupMutex);
			if (m_StartupComplete) return;

			m_StartupError = error;
			m_StartupComplete = true;
		}

		m_StartupCondition.notify_all();
	}

	bool WindowsNetworkBackend::IsStartupComplete() const {
		std::scoped_lock lock(m_StartupMutex);
		return m_StartupComplete;
	}
}

namespace Aurore::Network::Detail {
	std::unique_ptr<NetworkBackend> CreateWindowsIocpBackend() {
		return std::make_unique<Windows::WindowsNetworkBackend>();
	}
}
