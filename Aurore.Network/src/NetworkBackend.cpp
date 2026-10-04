#include "NetworkBackend.hpp"

#include <Aurore/Build/Config.hpp>

#if AURORE_NETWORK_BACKEND_IOCP
#include "windows/WindowsBackend.hpp"
#endif

#if AURORE_NETWORK_BACKEND_EPOLL
#include "linux/LinuxBackend.hpp"
#endif

#include <memory>

namespace Aurore::Network::Detail {
	NetworkResult<std::unique_ptr<NetworkBackend>>
	CreateNetworkBackend(
		NetworkBackendType backend_type) {

		switch (backend_type) {
		case NetworkBackendType::Automatic:
#if AURORE_NETWORK_BACKEND_IOCP
			return CreateWindowsIocpBackend();
#elif AURORE_NETWORK_BACKEND_EPOLL
			return CreateLinuxEpollBackend();
#else
			return std::unexpected(
				NetworkError::BackendUnavailable);
#endif

		case NetworkBackendType::Iocp:
#if AURORE_NETWORK_BACKEND_IOCP
			return CreateWindowsIocpBackend();
#else
			return std::unexpected(
				NetworkError::BackendUnavailable);
#endif

		case NetworkBackendType::Epoll:
#if AURORE_NETWORK_BACKEND_EPOLL
			return CreateLinuxEpollBackend();
#else
			return std::unexpected(
				NetworkError::BackendUnavailable);
#endif

		case NetworkBackendType::Kqueue:
			return std::unexpected(
				NetworkError::BackendUnavailable);
		}

		return std::unexpected(
			NetworkError::BackendUnavailable);
	}
}
