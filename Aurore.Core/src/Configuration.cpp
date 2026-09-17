#include <Aurore/Core/Configuration.hpp>

#include <fstream>

namespace Aurore::Core {
	void Configuration::CreateDefaults() {
		Set("Server.Name", "Aurore");

		Set("Network.Backend", "Automatic");
		Set("Network.BindAddress", "0.0.0.0");
		Set("Network.Port", 25565);
		Set("Network.MaximumConnections", 1024);
		Set("Network.ReceiveBufferSize", 64 * 1024);
		Set("Network.MaximumInboundBytesPerConnection", 2 * 1024 * 1024);
		Set("Network.MaximumOutboundBytesPerConnection", 2 * 1024 * 1024);
		Set("Network.MaximumCommandQueueEntries", 16 * 1024);
		Set("Network.MaximumCommandQueueBytes", 64 * 1024 * 1024);
		Set("Network.MaximumEventQueueEntries", 16 * 1024);
		Set("Network.MaximumEventQueueBytes", 64 * 1024 * 1024);
		Set("Network.MaximumTotalOutboundBytes", 256 * 1024 * 1024);
		Set("Network.MaximumTotalInboundEventBytes", 256 * 1024 * 1024);

		Set("World.ViewDistance", 10);

		Set("Logging.Enabled", true);
		Set("Logging.Level", "Info");

		Set("Clients.MaximumClients", 1024);
		Set("Clients.MaxPlayers", 20);
		Set("Clients.HandshakeTimeoutMilliseconds", 10000);
		Set("Clients.LoginTimeoutMilliseconds", 30000);
		Set("Clients.ConfigurationTimeoutMilliseconds", 30000);
		Set("Clients.IdleTimeoutMilliseconds", 30000);
	}

	ConfigResult Configuration::Load(const std::filesystem::path& path) {
		m_Path = path;

		std::ifstream file(path);

		if (!file) return { false, "Unable to open configuration file" };

		auto result = Sonnet::parse(file);

		if (!result) return { false, result.error().msg };

		m_Data = std::move(result.value());

		return { true, {} };
	}

	ConfigResult Configuration::Save(const std::filesystem::path& path) const {
		const auto parent = path.parent_path();
		if (!parent.empty() && !std::filesystem::exists(parent))
			std::filesystem::create_directories(parent);

		std::ofstream file(path);

		if (!file) return { false, "Unable to open configuration file for writing" };

		file << Sonnet::dump(m_Data, { .pretty = true, .indent = 4 });

		return { true, {} };
	}

	bool Configuration::Exists(std::string_view key) const {
		return Find(key) != nullptr;
	}

	Sonnet::value* Configuration::GetOrCreate(std::string_view key) {
		Sonnet::value* current{ &m_Data };
		size_t start{ 0 };

		while (start < key.size()) {
			size_t end = key.find('.', start);

			if (end == std::string_view::npos)
				end = key.size();

			std::string_view part = key.substr(start, end - start);

			current = &(*current)[std::string(part)];

			start = end + 1;
		}

		return current;
	}

	Sonnet::value* Configuration::Find(std::string_view key) {
		Sonnet::value* current{ &m_Data };
		size_t start{ 0 };

		while (start < key.size()) {
			size_t end = key.find('.', start);

			if (end == std::string_view::npos)
				end = key.size();

			std::string_view part = key.substr(start, end - start);

			auto probe = current->find(part);
			if (!probe) return nullptr;

			current = &(*current)[std::string(part)];
			if (!current) return nullptr;

			start = end + 1;
		}

		return current;
	}

	const Sonnet::value* Configuration::Find(std::string_view key) const {
		const Sonnet::value* current{ &m_Data };
		size_t start{ 0 };

		while (start < key.size()) {
			size_t end = key.find('.', start);

			if (end == std::string_view::npos)
				end = key.size();

			std::string_view part = key.substr(start, end - start);

			current = current->find(part);
			if (!current) return nullptr;

			start = end + 1;
		}

		return current;
	}

} // namespace Aurore::Core
