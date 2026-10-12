#pragma once

#include <Aurore/Core/ClientLifecycle.hpp>
#include <Aurore/Core/ClientLogin.hpp>
#include <Aurore/Core/KeepAliveTracker.hpp>
#include <Aurore/Core/PlayerSynchronizationTracker.hpp>
#include <Aurore/Core/RegistrySnapshotStore.hpp>

#include <Aurore/Network/NetworkManager.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/World/Player.hpp>
#include <Aurore/World/WorldManager.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Aurore::Core {
	class ClientService final {
	public:
		ClientService(Network::NetworkManager& network_manager,
			RegistrySnapshotStore& registry_snapshots,
			World::WorldManager& world_manager) noexcept;

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
		[[nodiscard]] World::Player* FindPlayer(Network::ConnectionId connection) noexcept;
		[[nodiscard]] const World::Player* FindPlayer(Network::ConnectionId connection) const noexcept;
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
		[[nodiscard]] bool HandleTeleportAcknowledgedRequest(ClientConnection& client, const Protocol::TeleportAcknowledgedRequest& request);
		[[nodiscard]] bool HandleChunkBatchFeedbackRequest(ClientConnection& client, const Protocol::ChunkBatchFeedbackRequest& request);
		[[nodiscard]] bool HandleKeepAliveResponseRequest(ClientConnection& client, const Protocol::KeepAliveResponseRequest& request);
		[[nodiscard]] bool HandlePlayerMovementRequest(ClientConnection& client, const Protocol::PlayerMovementRequest& request);
		[[nodiscard]] bool HandlePlayerInputRequest(ClientConnection& client, const Protocol::PlayerInputRequest& request);
		[[nodiscard]] bool HandlePlayerLoadedRequest(ClientConnection& client, const Protocol::PlayerLoadedRequest& request);

		void RemoveWorldPlayer(Network::ConnectionId connection) noexcept;

		Network::NetworkManager& m_NetworkManager;
		RegistrySnapshotStore& m_RegistrySnapshots;
		World::WorldManager& m_WorldManager;

		ConnectionManager m_ConnectionManager;
		ClientLogin m_ClientLogin;
		std::unordered_map<Network::ConnectionId, World::PlayerEntityId> m_PlayerConnections;
		std::unordered_map<Network::ConnectionId, KeepAliveTracker> m_KeepAliveTrackers;
		std::unordered_map<Network::ConnectionId, PlayerSynchronizationTracker> m_PlayerSynchronizationStates;
		KeepAliveTracker::Durations m_KeepAliveDurations{};
		std::int64_t m_NextKeepAliveId{ 1 };

		Protocol::ConfigurationSequencePolicy m_ConfigurationPolicy;
		Protocol::Packets::Configuration::Limits m_ConfigurationLimits{ Protocol::Packets::Configuration::DefaultLimits };
		Protocol::ServerStatus m_ServerStatus;

		bool m_FatalNetworkFailure{ false };
	};
}
