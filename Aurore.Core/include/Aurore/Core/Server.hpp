#pragma once

#include <Aurore/Core/ClientService.hpp>
#include <Aurore/Core/Configuration.hpp>
#include <Aurore/Core/Tick.hpp>
#include <Aurore/Core/RegistrySnapshotStore.hpp>

#include <Aurore/Network/NetworkManager.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>

#include <Aurore/World/WorldManager.hpp>

#include <atomic>
#include <cstddef>

namespace Aurore::Core::Detail {
	class ServerTestAccess;
}

namespace Aurore::Core {
	class Server final {
	public:
		Server() noexcept;
		~Server() noexcept;

		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		Server(Server&&) = delete;
		Server& operator=(Server&&) = delete;

		[[nodiscard]] int Run();

		void RequestStop() noexcept;

	private:
		friend class Detail::ServerTestAccess;

		[[nodiscard]] bool Initialize();

		[[nodiscard]] bool LoadConfiguration();
		[[nodiscard]] bool InitializeDataFoundations();
		[[nodiscard]] bool InitializeProtocolConfiguration();
		[[nodiscard]] bool InitializeClientManagement();
		[[nodiscard]] bool InitializeNetworking();

		[[nodiscard]] std::optional<Network::NetworkConfiguration> BuildNetworkConfiguration() const;
		[[nodiscard]] std::optional<ClientConfiguration> BuildClientConfiguration() const;
		[[nodiscard]] Protocol::ServerStatus BuildServerStatus(const ClientConfiguration& client_configuration) const;

		void RunLoop();

		void RunTick(const TickSchedule::Context& context);
		void RunTickFunctions(const TickSchedule::Context& context);
		void SendTimeUpdates(const TickSchedule::Context& context);
		void RunDimensions(const TickSchedule::Context& context);
		void RunPlayerNetworking(const TickSchedule::Context& context);
		void SendPlayerInformation(const TickSchedule::Context& context);
		void RunAutosave(const TickSchedule::Context& context);
		void RunPendingTasks(const TickSchedule::Context& context);

		void Shutdown() noexcept;

		Configuration m_Config;
		TickSchedule m_TickSchedule;
		RegistrySnapshotStore m_RegistrySnapshots;
		Protocol::ConfigurationSequencePolicy m_ConfigurationPolicy;
		Protocol::Packets::Configuration::Limits m_ConfigurationLimits{ Protocol::Packets::Configuration::DefaultLimits };
		World::WorldManager m_WorldManager;

		Network::NetworkManager m_NetworkManager;
		ClientService m_ClientService;

		std::atomic_bool m_Running{ false };

		bool m_LoggingInitialized{ false };
		bool m_StopHandlerInstalled{ false };
		bool m_Initialized{ false };
	};
}
