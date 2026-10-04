#pragma once

#ifndef __linux__
#error LinuxBackend.hpp is only available on Linux.
#endif

#include "../NetworkBackend.hpp"
#include "LinuxTypes.hpp"
#include "LinuxUtils.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace Aurore::Network::Detail {
	[[nodiscard]] std::unique_ptr<NetworkBackend> CreateLinuxEpollBackend();
}

namespace Aurore::Network::Detail::Linux {
	class LinuxNetworkBackend final : public NetworkBackend {
	public:
	LinuxNetworkBackend() = default;
	~LinuxNetworkBackend() noexcept override;

	[[nodiscard]] NetworkResult<void> Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) override;

	[[nodiscard]] NetworkResult<NetworkEndpoint> Start() override;

	void NotifyCommandAvailable() noexcept override;
	void Stop() noexcept override;
	void Shutdown() noexcept override;

	private:
		/*
		 * StartWorker() is intentionally separated from Start().
		 * Step 2 will create and register the listening socket first,
		 * then start the worker and return the real bound endpoint.
		 */
		[[nodiscard]] NetworkResult<void> StartWorker();

		void StopWorker() noexcept;
		void WorkerMain() noexcept;

		void HandleWorkerFailure(int error, std::string_view operation) noexcept;

		void EmitFailure(NetworkError error, std::string message, bool fatal) noexcept;

		void SignalStartup(std::optional<NetworkError> error) noexcept;

		void SignalStartupFailureNoexcept() noexcept;

		[[nodiscard]] bool IsStartupComplete() const noexcept;

		NetworkConfiguration m_Config;

		NetworkCommandQueue* m_Commands{ nullptr };
		NetworkEventQueue* m_Events{ nullptr };
		NetworkResourceLedger* m_Resources{ nullptr };

		UniqueFd m_Epoll;
		UniqueFd m_CommandEvent;

		std::thread m_Worker;
		std::atomic_bool m_StopRequested{ false };
		std::atomic_bool m_Started{ false };

		mutable std::mutex m_StartupMutex;
		std::condition_variable m_StartupCondition;
		bool m_StartupComplete{ false };
		std::optional<NetworkError> m_StartupError;

		bool m_Initialized{ false };
		bool m_WorkerFailureReported{ false };
	};
}
