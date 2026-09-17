#pragma once

#include <Aurore/Network/NetworkManager.hpp>

#include "NetworkBackend.hpp"

#include <memory>
#include <utility>

namespace Aurore::Network::Detail {
	class NetworkManagerTestAccess final {
	public:
		[[nodiscard]] static bool InstallBackend(NetworkManager& manager, std::unique_ptr<NetworkBackend> backend) {
			return manager.InstallBackendForTesting(std::move(backend));
		}

		[[nodiscard]] static NetworkResourceSnapshot GetResourceSnapshot(const NetworkManager& manager) noexcept {
			return manager.GetResourceSnapshotForTesting();
		}

		[[nodiscard]] static NetworkQueueSnapshot GetCommandQueueSnapshot(const NetworkManager& manager) noexcept {
			return manager.GetCommandQueueSnapshotForTesting();
		}

		[[nodiscard]] static NetworkQueueSnapshot GetEventQueueSnapshot(const NetworkManager& manager) noexcept {
			return manager.GetEventQueueSnapshotForTesting();
		}
	};
}

