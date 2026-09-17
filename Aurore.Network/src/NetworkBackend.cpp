#include "NetworkBackend.hpp"

#if defined(_WIN32)
#include "windows/WindowsBackend.hpp"
#else
// TODO: Linux network backend
#endif

#include <memory>
#include <utility>

namespace Aurore::Network::Detail {
	namespace {
		class UnavailableNetworkBackend final : public NetworkBackend {
		public:
			[[nodiscard]] NetworkResult<void> Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) override {
				m_Configuration = config;
				m_Commands = &commands;
				m_Events = &events;
				m_Resources = &resources;
				m_Initialized = true;
				return {};
			}

			[[nodiscard]] NetworkResult<NetworkEndpoint> Start() override {
				if (!m_Initialized) return std::unexpected(NetworkError::NotInitialized);
				if (m_Events != nullptr) {
					[[maybe_unused]] const auto result = m_Events->Push(QueuedNetworkEvent{
						.Event = NetworkFailureEvent{
							.Error = NetworkError::BackendUnavailable,
							.Message = "No platform network backend has been installed",
							.Fatal = true,
						},
						.InboundReservation = {},
					});
				}
				return std::unexpected(NetworkError::BackendUnavailable);
			}

			void NotifyCommandAvailable() noexcept override {}
			void Stop() noexcept override {}

			void Shutdown() noexcept override {
				m_Commands = nullptr;
				m_Events = nullptr;
				m_Resources = nullptr;
				m_Initialized = false;
				m_Configuration = {};
			}

		private:
			NetworkConfiguration m_Configuration;
			NetworkCommandQueue* m_Commands{ nullptr };
			NetworkEventQueue* m_Events{ nullptr };
			NetworkResourceLedger* m_Resources{ nullptr };
			bool m_Initialized{ false };
		};
	}

	NetworkResult<std::unique_ptr<NetworkBackend>> CreateNetworkBackend(NetworkBackendType backend_type) {
		switch (backend_type) {
		case NetworkBackendType::Automatic:
#if defined(_WIN32)
			return CreateWindowsIocpBackend();
#else
			return std::unexpected(NetworkError::BackendUnavailable);
#endif
		case NetworkBackendType::Iocp:
#if defined(_WIN32)
			return CreateWindowsIocpBackend();
#else
			return std::unexpected(NetworkError::BackendUnavailable);
#endif
		case NetworkBackendType::Epoll:
		case NetworkBackendType::Kqueue: return std::unexpected(NetworkError::BackendUnavailable);
		}
		return std::unexpected(NetworkError::BackendUnavailable);
	}
}

