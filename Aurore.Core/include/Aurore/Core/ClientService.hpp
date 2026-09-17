#pragma once

#include <Aurore/Core/ClientLifecycle.hpp>
#include <Aurore/Core/ClientLogin.hpp>
#include <Aurore/Core/RegistrySnapshotStore.hpp>

#include <Aurore/Network/NetworkManager.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <cstddef>
#include <vector>

namespace Aurore::Core {
	class ClientService final {
	public:
		ClientService(Network::NetworkManager& network_manager, RegistrySnapshotStore& registry_snapshots) noexcept;

		ClientService(const ClientService&) = delete;
		ClientService& operator=(const ClientService&) = delete;

		ClientService(ClientService&&) = delete;
		ClientService& operator=(ClientService&&) = delete;

		[[nodiscard]] bool Initialize(ClientConfiguration client_configuration,
			Protocol::ConfigurationSequencePolicy configuration_policy,
			Protocol::Packets::Configuration::Limits configuration_limits,
			Protocol::ServerStatus server_status);

		[[nodiscard]] bool ProcessNetworkEvents();
		void ProcessTimeouts(ClientConnection::TimePoint now);

		void BeginShutdown() noexcept;
		void CompleteShutdown() noexcept;

		[[nodiscard]] std::size_t GetConnectionCount() const noexcept;
		[[nodiscard]] ClientConnection* FindClient(Network::ConnectionId connection) noexcept;
		[[nodiscard]] std::vector<ClientSnapshot> GetClientSnapshots() const;

	private:
		[[nodiscard]] Protocol::ServerStatus BuildServerStatus() const;

		void HandleNetworkEvent(const Network::ConnectionOpenedEvent& event);
		void HandleNetworkEvent(const Network::BytesReceivedEvent& event);
		void HandleNetworkEvent(const Network::ConnectionClosedEvent& event);
		void HandleNetworkEvent(const Network::NetworkFailureEvent& event);

		[[nodiscard]] bool ApplyClientResult(ClientConnection& client, ClientReceiveResult result);
		void RequestClientClose(ClientConnection& client, ClientReceiveDisposition disposition, ClientCloseCause cause);

		[[nodiscard]] bool HandleProtocolRequest(ClientConnection& client, const Protocol::ProtocolRequest& request);
		[[nodiscard]] bool HandleLoginStartRequest(ClientConnection& client, const Protocol::LoginStartRequest& request);
		[[nodiscard]] bool HandleConfigurationStartRequest(ClientConnection& client, const Protocol::ConfigurationStartRequest& request);
		[[nodiscard]] bool HandleConfigurationCompleteRequest(ClientConnection& client, const Protocol::ConfigurationCompleteRequest& request);

		Network::NetworkManager& m_NetworkManager;
		RegistrySnapshotStore& m_RegistrySnapshots;

		ConnectionManager m_ConnectionManager;
		ClientLogin m_ClientLogin;

		Protocol::ConfigurationSequencePolicy m_ConfigurationPolicy;
		Protocol::Packets::Configuration::Limits m_ConfigurationLimits{ Protocol::Packets::Configuration::DefaultLimits };
		Protocol::ServerStatus m_ServerStatus;

		bool m_FatalNetworkFailure{ false };
	};
}

