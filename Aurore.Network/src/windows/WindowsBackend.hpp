#pragma once

#if !defined(_WIN32)
#error WindowsNetworkBackend.hpp is only available on Windows.
#endif

#include "../NetworkBackend.hpp"
#include "WindowsTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Aurore::Network::Detail {
	std::unique_ptr<NetworkBackend> CreateWindowsIocpBackend();
}

namespace Aurore::Network::Detail::Windows {
	class WindowsNetworkBackendTestAccess;

	class WindowsNetworkBackend final : public NetworkBackend {
	public:
		WindowsNetworkBackend() = default;
		~WindowsNetworkBackend() noexcept override;

		[[nodiscard]] NetworkResult<void> Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) override;
		[[nodiscard]] NetworkResult<NetworkEndpoint> Start() override;

		void NotifyCommandAvailable() noexcept override;
		void Stop() noexcept override;
		void Shutdown() noexcept override;

	private:
		friend class WindowsNetworkBackendTestAccess;

		enum class WorkerStepResult : std::uint8_t {
			Continue,
			Exit,
		};

		struct WorkerCompletion final {
			bool Succeeded{ false };
			DWORD TransferredBytes{ 0 };
			ULONG_PTR CompletionKey{ 0 };
			OVERLAPPED* Overlapped{ nullptr };
			DWORD Error{ ERROR_SUCCESS };
		};

		[[nodiscard]] WorkerStepResult ProcessCompletion(const WorkerCompletion& completion);
		[[nodiscard]] NetworkResult<NetworkEndpoint> CreateListener();
		[[nodiscard]] bool LoadExtensionFunctions() noexcept;
		[[nodiscard]] bool AssociateSocket(SOCKET socket) noexcept;

		void WorkerMain() noexcept;
		void HandleControlCompletion(CompletionKey key);
		void HandleIoCompletion(std::unique_ptr<IoOperation> operation, DWORD transferred_bytes, DWORD error);
		void HandleAcceptCompletion(std::unique_ptr<AcceptOperation> operation, DWORD transferred_bytes, DWORD error);
		void HandleReceiveCompletion(std::unique_ptr<ReceiveOperation> operation, DWORD transferred_bytes, DWORD error);
		void HandleSendCompletion(std::unique_ptr<SendOperation> operation, DWORD transferred_bytes, DWORD error);
		void HandleWorkerException(std::string_view message) noexcept;
		void BeginEmergencyShutdown() noexcept;
		void SignalStartupFailureNoexcept() noexcept;

		void HandleStartup();
		void DrainCommands();

		void HandleCommand(SendCommand&& command);
		void HandleCommand(const CloseAfterFlushCommand& command);
		void HandleCommand(const CloseImmediatelyCommand& command);
		void HandleCommand(const ResumeReceiveCommand& command);
		void HandleCommand(const StopCommand& command);

		[[nodiscard]] bool PostInitialAccepts();
		[[nodiscard]] bool PostAccept();
		[[nodiscard]] bool PostReceive(ConnectionId connection);

		void StartNextSend(ConnectionId connection);
		void TryCompleteCloseAfterFlush(ConnectionId connection);
		[[nodiscard]] bool CancelPendingReceiveForClose(ConnectionId connection);
		[[nodiscard]] bool SubmitSendOperation(ConnectionId connection, std::unique_ptr<SendOperation> operation);
		[[nodiscard]] std::unique_ptr<IoOperation> TakeOperation(OVERLAPPED* overlapped);
		[[nodiscard]] ConnectionId AllocateConnectionId() noexcept;

		void CloseConnection(ConnectionId connection, ConnectionCloseReason reason, std::string detail, bool abortive);
		void TryRetireConnection(ConnectionId connection);
		void BeginShutdown();

		[[nodiscard]] QueuePushResult PushEvent(NetworkEvent event, NetworkResourceLedger::Reservation reservation = {});
		void EmitFailure(NetworkError error, std::string message, bool fatal);
		void SignalStartup(std::optional<NetworkError> error);
		[[nodiscard]] bool IsStartupComplete() const;

		NetworkConfiguration m_Config;
		NetworkCommandQueue* m_Commands{ nullptr };
		NetworkEventQueue* m_Events{ nullptr };
		NetworkResourceLedger* m_Resources{ nullptr };

		WinsockSession m_Winsock;
		UniqueHandle m_CompletionPort;
		UniqueSocket m_Listener;

		int m_ListenerFamily{ AF_UNSPEC };
		LPFN_ACCEPTEX m_AcceptEx{ nullptr };
		LPFN_GETACCEPTEXSOCKADDRS m_GetAcceptExSockaddrs{ nullptr };

		std::optional<NetworkEndpoint> m_BoundEndpoint;
		std::unordered_map<ConnectionId, BackendConnection> m_Connections;
		std::unordered_map<OVERLAPPED*, std::unique_ptr<IoOperation>> m_Operations;

		std::thread m_Worker;
		std::atomic_bool m_StopRequested{ false };
		std::atomic_bool m_Started{ false };

		bool m_Initialized{ false };
		bool m_ShuttingDown{ false };
		bool m_WorkerFailureReported{ false };
		std::uint64_t m_NextConnectionId{ 1 };

		mutable std::mutex m_StartupMutex;
		std::condition_variable m_StartupCondition;
		bool m_StartupComplete{ false };
		std::optional<NetworkError> m_StartupError;
	};
}

