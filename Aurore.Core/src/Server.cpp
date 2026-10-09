#include <Aurore/Core/Server.hpp>

#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>

#include <Aurore/Util/ResourceLocation.hpp>

#include <Aurore/Core/logger/Log.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#endif // if defined _WIN32

namespace {
	const std::filesystem::path DefaultConfigurationPath{ "config/aurore.json" };

#if defined(_WIN32)
	std::atomic<Aurore::Core::Server*> ActiveServer{ nullptr };

	BOOL WINAPI HandleConsoleControl(DWORD control_type) {
		switch (control_type) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		{
			auto* server = ActiveServer.load(std::memory_order_acquire);
			if (!server) return FALSE;
			server->RequestStop();
			return TRUE;
		}
		default: return FALSE;
		}
	}

	bool InstallStopHandler(Aurore::Core::Server& server) noexcept {
		Aurore::Core::Server* expected{ nullptr };

		if (!ActiveServer.compare_exchange_strong(expected, &server, std::memory_order_release, std::memory_order_relaxed))
			return false;

		if (!SetConsoleCtrlHandler(HandleConsoleControl, TRUE)) {
			ActiveServer.store(nullptr, std::memory_order_release);
			return false;
		}
		return true;
	}

	void RemoveStopHandler(Aurore::Core::Server& server) noexcept {
		SetConsoleCtrlHandler(HandleConsoleControl, FALSE);

		auto* expected{ &server };
		ActiveServer.compare_exchange_strong(expected, nullptr, std::memory_order_release, std::memory_order_relaxed);
	}
#else
	bool InstallStopHandler(Aurore::Core::Server&) noexcept {
		return true;
	}

	void RemoveStopHandler(Aurore::Core::Server&) noexcept {}
#endif

	std::optional<Aurore::Network::NetworkBackendType> ParseNetworkBackend(std::string_view name) {
		using Aurore::Network::NetworkBackendType;

		if (name == "Automatic") return NetworkBackendType::Automatic;
		if (name == "Iocp") return NetworkBackendType::Iocp;
		if (name == "Epoll") return NetworkBackendType::Epoll;
		// For later implementation:
		// if (name == "Kqueue") return NetworkBackendType::Kqueue;

		return std::nullopt;
	}

}

namespace Aurore::Core {
	Server::Server() noexcept
		: m_ClientService(m_NetworkManager, m_RegistrySnapshots) {}

	Server::~Server() noexcept {
		Shutdown();
	}

	int Server::Run() {
		int exit_code{ EXIT_FAILURE };
		try {
			if (!Initialize()) {
				Shutdown();
				return EXIT_FAILURE;
			}
			RunLoop();
			exit_code = EXIT_SUCCESS;
		}
		catch (const std::exception& ex) {
			if (m_LoggingInitialized)
				AU_ERROR(LogCategory::Core, "Unhandled Server Exception: {}", ex.what());
		}
		catch (...) {
			if (m_LoggingInitialized)
				AU_ERROR(LogCategory::Core, "Unhandled unknown exception!");
		}

		Shutdown();
		std::cin.get();
		return exit_code;
	}

	void Server::RequestStop() noexcept {
		m_Running.store(false, std::memory_order_release);
	}

	bool Server::Initialize() {
		if (m_Initialized) return true;

		Log::Initialize();
		m_LoggingInitialized = true;

		AU_INFO(LogCategory::Core, "Initializing Aurore Server.");

		if (!LoadConfiguration()) return false;

		if (!InitializeDataFoundations()) {
			AU_ERROR(LogCategory::Registry, "Failed to initialize data foundations!");
			return false;
		}

		if (!InitializeProtocolConfiguration()) {
			AU_ERROR(LogCategory::Protocol, "Failed to initialize Configuration policy!");
			return false;
		}

		const auto active_snapshot = m_RegistrySnapshots.GetActiveSnapshot();
		AU_INFO(LogCategory::Registry, "Registry snapshot generation {} is active",
			active_snapshot->GetGeneration());

		if (!InitializeClientManagement()) {
			AU_ERROR(LogCategory::Core, "Failed to initialize client management!");
			return false;
		}

		m_TickSchedule = TickSchedule{};

		if (!m_WorldManager.Initialize()) {
			AU_ERROR(LogCategory::Core, "Failed to initialize world management!");
			return false;
		}

		m_Running.store(true, std::memory_order_release);

		if (!InstallStopHandler(*this)) {
			m_Running.store(false, std::memory_order_release);
			AU_ERROR(LogCategory::Core, "Failed to install the console stop handler");
			return false;
		}

		m_StopHandlerInstalled = true;

		if (!InitializeNetworking()) {
			m_Running.store(false, std::memory_order_release);
			AU_ERROR(LogCategory::Core, "Failed to initialize network management!");
			return false;
		}

		m_Initialized = true;
		AU_INFO(LogCategory::Core, "Aurore Server initialization complete");
		return true;
	}

	bool Server::LoadConfiguration() {
		std::error_code filesystem_error;

		const bool configuration_exists = std::filesystem::exists(DefaultConfigurationPath, filesystem_error);
		if (filesystem_error) {
			AU_ERROR(LogCategory::Core, "Failed to inspect configuration path '{}': {}", DefaultConfigurationPath.string(), filesystem_error.message());
			return false;
		}

		if (!configuration_exists) {
			AU_INFO(LogCategory::Core, "No configuration file was found. Creating defaults at '{}'.", DefaultConfigurationPath.string());
			m_Config.CreateDefaults();
			const auto save_result = m_Config.Save(DefaultConfigurationPath);
			if (!save_result) {
				AU_ERROR(LogCategory::Core, "Failed to save the default configuration: {}", save_result.Error);
				return false;
			}
			AU_INFO(LogCategory::Core, "Created the default configuration");
			return true;
		}

		const auto load_result = m_Config.Load(DefaultConfigurationPath);
		if (!load_result) {
			AU_ERROR(LogCategory::Core, "Failed to load configuration '{}': {}", DefaultConfigurationPath.string(), load_result.Error);
			return false;
		}

		AU_INFO(LogCategory::Core, "Loaded configuration from '{}'", DefaultConfigurationPath.string());
		return true;
	}

	bool Server::InitializeDataFoundations() {
		if (m_RegistrySnapshots.GetActiveSnapshot() != nullptr) return true;
		auto snapshot = SyntheticRegistrySnapshotFactory::Build(InitialSyntheticRegistryGeneration);
		if (!snapshot) return false;

		const auto publication = m_RegistrySnapshots.Publish(std::move(*snapshot));
		if (publication) return true;

		return m_RegistrySnapshots.GetActiveGeneration() == InitialSyntheticRegistryGeneration;
	}

	bool Server::InitializeProtocolConfiguration() {
		auto vanilla_feature = Util::ResourceLocation::Parse("minecraft:vanilla");
		if (!vanilla_feature) {
			AU_ERROR(LogCategory::Protocol,
				"Built-in Configuration feature identifier is invalid. Error: {}, offset: {}",
				static_cast<unsigned int>(vanilla_feature.error().Code), vanilla_feature.error().Offset);
			return false;
		}

		Protocol::ConfigurationSequencePolicy policy;
		policy.EnabledFeatures.emplace_back(std::move(*vanilla_feature));

		/* The synthetic Configuration sequence currently offers no known packs. */
		policy.KnownPacks.clear();

		m_ConfigurationPolicy = std::move(policy);
		m_ConfigurationLimits = Protocol::Packets::Configuration::DefaultLimits;
		return true;
	}

	bool Server::InitializeClientManagement() {
		const auto config = BuildClientConfiguration();

		if (!config.has_value()) {
			AU_ERROR(LogCategory::Core, "Client configuration is invalid!");
			return false;
		}

		if (!m_ClientService.Initialize(
			*config,
			m_ConfigurationPolicy,
			m_ConfigurationLimits,
			BuildServerStatus(*config))) {
			AU_ERROR(LogCategory::Core, "Failed to apply client configuration!");
			return false;
		}
		return true;
	}

	bool Server::InitializeNetworking() {
		const auto config = BuildNetworkConfiguration();
		if (!config.has_value()) {
			AU_ERROR(LogCategory::Core, "Network configuration is invalid!");
			return false;
		}
		const auto init_res = m_NetworkManager.Initialize(*config);

		if (!init_res) {
			AU_ERROR(LogCategory::Core, "Failed to initialize networking. Error: {}", static_cast<unsigned int>(init_res.error()));
			return false;
		}

		const auto start_res = m_NetworkManager.Start();
		if (!start_res) {
			AU_ERROR(LogCategory::Core, "Failed to start networking. Error code: {}", static_cast<unsigned int>(start_res.error()));
			return false;
		}

		AU_INFO(LogCategory::Core, "Network listener started on {}:{}", start_res->Address, start_res->Port);
		return true;
	}

	std::optional<Network::NetworkConfiguration> Server::BuildNetworkConfiguration() const {
		auto read_setting = [this]<typename T>(std::string_view key, T default_value) -> std::optional<T> {
			if (!m_Config.Exists(key)) return default_value;
			auto value = m_Config.Get<T>(key);
			if (!value.has_value())
				AU_ERROR(LogCategory::Core, "Configuration setting '{}' has an invalid type or value!", key);
			return value;
		};

		const auto backend_name = read_setting("Network.Backend", std::string{ "Automatic" });
		const auto bind_address = read_setting("Network.BindAddress", std::string{ "0.0.0.0" });
		const auto port = read_setting("Network.Port", std::uint16_t{ 25565 });
		const auto maximum_connections = read_setting("Network.MaximumConnections", std::size_t{ 1024 });
		const auto receive_buffer_size = read_setting("Network.ReceiveBufferSize", std::size_t{ 64 * 1024 });
		const auto maximum_inbound_bytes = read_setting("Network.MaximumInboundBytesPerConnection", std::size_t{ 2 * 1024 * 1024 });
		const auto maximum_outbound_bytes = read_setting("Network.MaximumOutboundBytesPerConnection", std::size_t{ 2 * 1024 * 1024 });
		const auto maximum_command_queue_entries = read_setting("Network.MaximumCommandQueueEntries", std::size_t{ 16 * 1024 });
		const auto maximum_command_queue_bytes = read_setting("Network.MaximumCommandQueueBytes", std::size_t{ 64 * 1024 * 1024 });
		const auto maximum_event_queue_entries = read_setting("Network.MaximumEventQueueEntries", std::size_t{ 16 * 1024 });
		const auto maximum_event_queue_bytes = read_setting("Network.MaximumEventQueueBytes", std::size_t{ 64 * 1024 * 1024 });
		const auto maximum_total_outbound_bytes = read_setting("Network.MaximumTotalOutboundBytes", std::size_t{ 256 * 1024 * 1024 });
		const auto maximum_total_inbound_event_bytes = read_setting("Network.MaximumTotalInboundEventBytes", std::size_t{ 256 * 1024 * 1024 });

		if (!backend_name || !bind_address || !port || !maximum_connections || !receive_buffer_size
			|| !maximum_inbound_bytes || !maximum_outbound_bytes || !maximum_command_queue_entries
			|| !maximum_command_queue_bytes || !maximum_event_queue_entries || !maximum_event_queue_bytes
			|| !maximum_total_outbound_bytes || !maximum_total_inbound_event_bytes)
			return std::nullopt;

		if (*maximum_connections == 0 || *receive_buffer_size == 0 || *maximum_inbound_bytes == 0
			|| *maximum_outbound_bytes == 0 || *maximum_command_queue_entries == 0
			|| *maximum_command_queue_bytes == 0 || *maximum_event_queue_entries == 0
			|| *maximum_event_queue_bytes == 0 || *maximum_total_outbound_bytes == 0
			|| *maximum_total_inbound_event_bytes == 0)
			return std::nullopt;

		const auto backend = ParseNetworkBackend(*backend_name);
		if (!backend.has_value()) {
			AU_ERROR(LogCategory::Core, "Unknown network backend: '{}'", *backend_name);
			return std::nullopt;
		}

		Network::NetworkConfiguration config;
		config.Backend = *backend;
		config.BindAddress = *bind_address;
		config.Port = *port;
		config.MaximumConnections = *maximum_connections;
		config.ReceiveBufferSize = *receive_buffer_size;
		config.MaximumInboundBytesPerConnection = *maximum_inbound_bytes;
		config.MaximumOutboundBytesPerConnection = *maximum_outbound_bytes;
		config.MaximumCommandQueueEntries = *maximum_command_queue_entries;
		config.MaximumCommandQueueBytes = *maximum_command_queue_bytes;
		config.MaximumEventQueueEntries = *maximum_event_queue_entries;
		config.MaximumEventQueueBytes = *maximum_event_queue_bytes;
		config.MaximumTotalOutboundBytes = *maximum_total_outbound_bytes;
		config.MaximumTotalInboundEventBytes = *maximum_total_inbound_event_bytes;

		return config;
	}

	std::optional<ClientConfiguration> Server::BuildClientConfiguration() const {
		auto read_setting = [this]<typename T>(std::string_view key, T default_value) -> std::optional<T> {
			if (!m_Config.Exists(key)) return default_value;
			auto value = m_Config.Get<T>(key);
			if (!value.has_value())
				AU_ERROR(LogCategory::Core, "Configuration setting '{}' has an invalid type or value!", key);
			return value;
		};

		const auto maximum_clients = read_setting("Clients.MaximumClients", std::size_t{ 1024 });
		const auto maximum_players = read_setting("Clients.MaxPlayers", std::size_t{ 20 });
		const auto handshake_timeout_ms = read_setting("Clients.HandshakeTimeoutMilliseconds", std::int64_t{ 10'000 });
		const auto login_timeout_ms = read_setting("Clients.LoginTimeoutMilliseconds", std::int64_t{ 30'000 });
		const auto configuration_timeout_ms = read_setting("Clients.ConfigurationTimeoutMilliseconds", std::int64_t{ 30'000 });
		const auto idle_timeout_ms = read_setting("Clients.IdleTimeoutMilliseconds", std::int64_t{ 30'000 });
		const auto maximum_outbound_bytes = read_setting(
			"Network.MaximumOutboundBytesPerConnection",
			std::size_t{ 2 * 1024 * 1024 });

		const auto maximum_command_queue_bytes = read_setting(
			"Network.MaximumCommandQueueBytes",
			std::size_t{ 64 * 1024 * 1024 });

		const auto maximum_total_outbound_bytes = read_setting(
			"Network.MaximumTotalOutboundBytes",
			std::size_t{ 256 * 1024 * 1024 });

		if (!maximum_clients || !maximum_players || !handshake_timeout_ms
			|| !login_timeout_ms || !configuration_timeout_ms || !idle_timeout_ms
			|| !maximum_outbound_bytes || !maximum_command_queue_bytes
			|| !maximum_total_outbound_bytes)
			return std::nullopt;

		if (*maximum_clients == 0 || *maximum_players == 0 || *handshake_timeout_ms <= 0
			|| *login_timeout_ms <= 0 || *configuration_timeout_ms <= 0 || *idle_timeout_ms <= 0
			|| *maximum_outbound_bytes == 0 || *maximum_command_queue_bytes == 0
			|| *maximum_total_outbound_bytes == 0)
			return std::nullopt;

		ClientConfiguration config;
		config.MaximumClients = *maximum_clients;
		config.MaximumPlayers = *maximum_players;
		config.HandshakeTimeout = std::chrono::milliseconds{ *handshake_timeout_ms };
		config.LoginTimeout = std::chrono::milliseconds{ *login_timeout_ms };
		config.ConfigurationTimeout = std::chrono::milliseconds{ *configuration_timeout_ms };
		config.IdleTimeout = std::chrono::milliseconds{ *idle_timeout_ms };
		config.ProtocolPipeline.MaximumOutboundActionBytes = std::min({
			*maximum_outbound_bytes,
			*maximum_command_queue_bytes,
			*maximum_total_outbound_bytes,
		});

		return config;
	}

	Protocol::ServerStatus Server::BuildServerStatus(const ClientConfiguration& client_configuration) const {
		Protocol::ServerStatus status;
		status.MaximumPlayers = static_cast<std::int32_t>(std::min<std::size_t>(
			client_configuration.MaximumPlayers,
			static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
		return status;
	}

	void Server::RunLoop() {
		AU_INFO(LogCategory::Core, "Beginning server execution loop");

		while (m_Running.load(std::memory_order_acquire)) {
			const auto current_time = TickSchedule::Clock::now();
			const auto context = m_TickSchedule.BeginTick(current_time);

			if (context.TimingReset)
				AU_WARN(LogCategory::Core, "Server tick {} was more than two seconds behind. Resetting tick timeline.", context.Number);

			RunTick(context);
			if (!m_Running.load(std::memory_order_acquire)) break;

			const auto after_tick = TickSchedule::Clock::now();
			if (after_tick < context.NextTickTime)
				std::this_thread::sleep_until(context.NextTickTime);
		}

		AU_DEBUG(LogCategory::Core, "Server execution loop stopped");
	}

	void Server::RunTick(const TickSchedule::Context& context) {
		RunTickFunctions(context);
		SendTimeUpdates(context);
		RunDimensions(context);
		RunPlayerNetworking(context);
		SendPlayerInformation(context);
		RunAutosave(context);
		RunPendingTasks(context);
	}

	void Server::RunTickFunctions(const TickSchedule::Context& context) {
		(void)context;
	}

	void Server::SendTimeUpdates(const TickSchedule::Context& context) {
		if ((context.Number % 20) != 0) return;
	}

	void Server::RunDimensions(const TickSchedule::Context& context) {
		m_WorldManager.RunTick(World::WorldTickContext{ .TickNumber = context.Number });
	}

	void Server::RunPlayerNetworking(const TickSchedule::Context& context) {
		(void)context;
		if (!m_ClientService.ProcessNetworkEvents())
			RequestStop();
		m_ClientService.ProcessTimeouts(ClientConnection::Clock::now());
	}

	void Server::SendPlayerInformation(const TickSchedule::Context& context) {
		(void)context;
	}

	void Server::RunAutosave(const TickSchedule::Context& context) {
		if ((context.Number % 6000) != 0) return;
	}

	void Server::RunPendingTasks(const TickSchedule::Context& context) {
		(void)context;
	}

	void Server::Shutdown() noexcept {
		m_Running.store(false, std::memory_order_release);

		m_ClientService.BeginShutdown();

		if (m_NetworkManager.IsInitialized())
			m_NetworkManager.Shutdown();

		m_ClientService.CompleteShutdown();

		if (m_StopHandlerInstalled) {
			RemoveStopHandler(*this);
			m_StopHandlerInstalled = false;
		}

		if (m_WorldManager.IsInitialized())
			m_WorldManager.Shutdown();

		if (m_LoggingInitialized) {
			try {
				if (m_Initialized)
					AU_INFO(LogCategory::Core, "Press Enter to exit.");
				Log::Shutdown();
			}
			catch (...) {
				/* Logging failures must not escape destruction. */
			}
			m_LoggingInitialized = false;
		}

		m_Initialized = false;
	}
}
