#pragma once

#include <Aurore/Core/Server.hpp>

#include <cstddef>
#include <vector>

namespace Aurore::Core::Detail {
	class ServerTestAccess final {
	public:
		[[nodiscard]] static Network::NetworkManager& GetNetworkManager(Server& server) noexcept {
			return server.m_NetworkManager;
		}

		static void ProcessNetworkEvents(Server& server) {
			(void)server.m_ClientService.ProcessNetworkEvents();
		}

		[[nodiscard]] static std::size_t GetConnectionCount(const Server& server) noexcept {
			return server.m_ClientService.GetConnectionCount();
		}

		[[nodiscard]] static bool InitializeDataFoundations(Server& server) {
			return server.InitializeDataFoundations();
		}

		[[nodiscard]] static const RegistrySnapshotStore& GetRegistrySnapshotStore(const Server& server) noexcept {
			return server.m_RegistrySnapshots;
		}

		[[nodiscard]] static bool InitializeProtocolConfiguration(Server& server) {
			return server.InitializeProtocolConfiguration();
		}

		[[nodiscard]] static bool InitializeClientManagement(Server& server) {
			return server.InitializeClientManagement();
		}

		[[nodiscard]] static ClientConnection* FindClient(Server& server, Network::ConnectionId connection) noexcept {
			return server.m_ClientService.FindClient(connection);
		}

		[[nodiscard]] static std::vector<ClientSnapshot> GetClientSnapshots(const Server& server) {
			return server.m_ClientService.GetClientSnapshots();
		}
	};
}
