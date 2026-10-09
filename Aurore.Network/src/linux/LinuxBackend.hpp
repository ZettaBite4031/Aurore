#pragma once

#ifndef __linux__
#error LinuxBackend.hpp is only available on Linux.
#endif

#include "../NetworkBackend.hpp"
#include "LinuxTypes.hpp"
#include "LinuxUtils.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Aurore::Network::Detail {
	[[nodiscard]] std::unique_ptr<NetworkBackend> CreateLinuxEpollBackend();
}

namespace Aurore::Network::Detail::Linux {
	class LinuxNetworkBackend final : public NetworkBackend {
	public:
		LinuxNetworkBackend() = default;
		~LinuxNetworkBackend() noexcept override;

		[[nodiscard]] NetworkResult<void> Initialize(
			const NetworkConfiguration& config,
			NetworkCommandQueue& commands,
			NetworkEventQueue& events,
			NetworkResourceLedger& resources) override;

		[[nodiscard]] NetworkResult<NetworkEndpoint> Start() override;

		void NotifyCommandAvailable() noexcept override;
		void Stop() noexcept override;
		void Shutdown() noexcept override;

	private:
		[[nodiscard]] NetworkResult<NetworkEndpoint> CreateListener();
		[[nodiscard]] NetworkResult<void> StartWorker();

		void StopWorker() noexcept;
		void WorkerMain() noexcept;

		void HandleListenerEvent(std::uint32_t events);
		void AcceptConnections();

		void HandleConnectionEvent(ConnectionId connection, std::uint32_t events);
		void ReceiveAvailable(ConnectionId connection);
		void FlushOutbound(ConnectionId connection);

		void DrainCommands();
		void HandleCommand(SendCommand&& command);
		void HandleCommand(const CloseAfterFlushCommand& command);
		void HandleCommand(const CloseImmediatelyCommand& command);
		void HandleCommand(const ResumeReceiveCommand& command);
		void HandleCommand(const StopCommand& command);

		[[nodiscard]] std::uint32_t BuildConnectionEvents(const BackendConnection& connection) const noexcept;
		[[nodiscard]] bool UpdateConnectionInterest(ConnectionId connection);

		void TryCompleteCloseAfterFlush(ConnectionId connection);

		[[nodiscard]] ConnectionId AllocateConnectionId() noexcept;

		void CloseConnection(
			ConnectionId connection,
			ConnectionCloseReason reason,
			std::string detail,
			bool abortive);

		void BeginShutdown();

		[[nodiscard]] QueuePushResult PushEvent(
			NetworkEvent event,
			NetworkResourceLedger::Reservation reservation = {});

		void HandleWorkerFailure(int error, std::string_view operation) noexcept;
		void HandleWorkerException(std::string_view message) noexcept;

		void EmitFailure(NetworkError error, std::string message, bool fatal) noexcept;

		void SignalStartup(std::optional<NetworkError> error) noexcept;
		void SignalStartupFailureNoexcept() noexcept;

		NetworkConfiguration m_Config;

		NetworkCommandQueue* m_Commands{ nullptr };
		NetworkEventQueue* m_Events{ nullptr };
		NetworkResourceLedger* m_Resources{ nullptr };

		UniqueFd m_Epoll;
		UniqueFd m_CommandEvent;
		UniqueFd m_Listener;

		std::optional<NetworkEndpoint> m_BoundEndpoint;
		std::unordered_map<ConnectionId, BackendConnection> m_Connections;

		std::thread m_Worker;
		std::atomic_bool m_StopRequested{ false };
		std::atomic_bool m_Started{ false };

		mutable std::mutex m_StartupMutex;
		std::condition_variable m_StartupCondition;
		bool m_StartupComplete{ false };
		std::optional<NetworkError> m_StartupError;

		bool m_Initialized{ false };
		bool m_ShuttingDown{ false };
		bool m_WorkerFailureReported{ false };

		std::uint64_t m_NextConnectionId{ 1 };
	};
}
