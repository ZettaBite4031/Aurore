#include "ProductionBootstrap.hpp"

#include <Aurore/Core/Server.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {
	[[nodiscard]] std::optional<std::string> ReadEnvironmentVariable(const char* name) {
#if defined(_WIN32)
		std::size_t required_size{ 0 };
		if (getenv_s(&required_size, nullptr, 0, name) != 0 || required_size == 0)
			return std::nullopt;

		std::string value(required_size, '\0');
		std::size_t written{ 0 };

		if (getenv_s(&written, value.data(), value.size(), name) != 0 || written == 0)
			return std::nullopt;

		value.resize(written - 1);
		return value;
#else
		const char* value = std::getenv(name);
		if (value == nullptr || *value == '\0')
			return std::nullopt;

		return std::string{ value };
#endif
	}

	[[nodiscard]] Aurore::Server::ProductionBootstrapOptions BuildBootstrapOptions() {
		Aurore::Server::ProductionBootstrapOptions options;

		if (auto corpus_root = ReadEnvironmentVariable("AURORE_MINECRAFT_DATA_ROOT"))
			options.ExistingCorpusRoot = std::filesystem::path{ std::move(*corpus_root) };

		if (auto java = ReadEnvironmentVariable("AURORE_JAVA_EXECUTABLE"))
			options.JavaExecutable = std::filesystem::path{ std::move(*java) };

		return options;
	}

	void PrintBootstrapFailure(const Aurore::Server::ProductionBootstrapError& error) {
		std::cerr << "Failed to initialize Minecraft registry data: " << error.Message << '\n';

		if (!error.Path.empty())
			std::cerr << "Path: " << error.Path.string() << '\n';

		if (!error.Detail.empty())
			std::cerr << "Detail: " << error.Detail << '\n';
	}
}

int main() {
	const auto options = BuildBootstrapOptions();

	if (options.ExistingCorpusRoot)
		std::cout
			<< "Loading generated Minecraft "
			<< Aurore::Server::MinecraftVersion
			<< " data from "
			<< options.ExistingCorpusRoot->string()
			<< '\n';
	else
		std::cout
			<< "Preparing Minecraft "
			<< Aurore::Server::MinecraftVersion
			<< " registry data..."
			<< '\n';

	auto bootstrap = Aurore::Server::ProductionBootstrap::Build(options);
	if (!bootstrap) {
		PrintBootstrapFailure(bootstrap.error());
		return EXIT_FAILURE;
	}

	std::cout
		<< "Registry snapshot generation "
		<< Aurore::Server::InitialRegistryGeneration
		<< " prepared from "
		<< std::filesystem::relative(bootstrap->CorpusRoot).string()
		<< '\n';

	Aurore::Core::Server server{ std::move(bootstrap->Bootstrap) };
	return server.Run();
}
