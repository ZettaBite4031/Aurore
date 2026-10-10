#include <Aurore/Data/DataPreparation.hpp>
#include <Aurore/Data/DataPreparationService.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
	struct CommandLineOptions final {
		std::string MinecraftVersion;
		std::int32_t ProtocolVersion{ 0 };

		std::filesystem::path JarPath;
		std::filesystem::path OutputRoot;

		std::optional<std::filesystem::path> JavaExecutable;

		bool ListFiles{ false };
	};

	void PrintUsage() {
		std::cout
			<< "Aurore Data Tool\n\n"
			<< "Usage:\n"
			<< "  Aurore.DataTool prepare-raw"
			<< " --version <minecraft-version>"
			<< " --protocol <protocol-version>"
			<< " --jar <server.jar>"
			<< " --output <cache-root>"
			<< " [--java <java-executable>]"
			<< " [--list-files]\n\n"
			<< "Example:\n"
			<< "  Aurore.DataTool prepare-raw"
			<< " --version 1.21.11"
			<< " --protocol 774"
			<< " --jar C:\\Minecraft\\server.jar"
			<< " --output C:\\AuroreData\n";
	}

	[[nodiscard]] std::optional<std::string> ReadOptionValue(int argc, char** argv, int& index, std::string_view option) {
		if (index + 1 >= argc) {
			std::cerr << "Missing value for " << option << ".\n";
			return std::nullopt;
		}

		index++;
		return std::string{ argv[index] };
	}

	[[nodiscard]] bool ParseProtocolVersion(std::string_view value, std::int32_t& result) {
		const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
		return error == std::errc{} && end == value.data() + value.size();
	}

	[[nodiscard]] std::optional<CommandLineOptions> ParsePrepareRawOptions(int argc, char** argv) {
		CommandLineOptions options;
		for (int index{ 2 }; index < argc; index++) {
			const std::string_view argument{ argv[index] };
			if (argument == "--version") {
				auto value = ReadOptionValue(argc, argv, index, argument);
				if (!value) return std::nullopt;
				options.MinecraftVersion = std::move(*value);
				continue;
			}
			if (argument == "--protocol") {
				auto value = ReadOptionValue(argc, argv, index, argument);
				if (!value) return std::nullopt;
				if (!ParseProtocolVersion(*value, options.ProtocolVersion)) {
					std::cerr << "Invalid protocol version: " << *value << "\n";
					return std::nullopt;
				}
				continue;
			}
			if (argument == "--jar") {
				auto value = ReadOptionValue(argc, argv, index, argument);
				if (!value) return std::nullopt;
				options.JarPath = *value;
				continue;
			}
			if (argument == "--output") {
				auto value = ReadOptionValue(argc, argv, index, argument);
				if (!value) return std::nullopt;
				options.OutputRoot = *value;
				continue;
			}
			if (argument == "--java") {
				auto value = ReadOptionValue(argc, argv, index, argument);
				if (!value) return std::nullopt;
				options.JavaExecutable = std::filesystem::path{ *value };
				continue;
			}
			if (argument == "--list-files") {
				options.ListFiles = true;
				continue;
			}
			if (argument == "--help" || argument == "-h") {
				PrintUsage();
				return std::nullopt;
			}

			std::cerr << "Unknown option: " << argument << "\n";
			return std::nullopt;
		}

		if (options.MinecraftVersion.empty()) {
			std::cerr << "Missing required --version.\n";
			return std::nullopt;
		}
		if (options.ProtocolVersion <= 0) {
			std::cerr << "Missing or invalid required --protocol.\n";
			return std::nullopt;
		}
		if (options.JarPath.empty()) {
			std::cerr << "Missing required --jar.\n";
			return std::nullopt;
		}
		if (options.OutputRoot.empty()) {
			std::cerr << "Missing required --output.\n";
			return std::nullopt;
		}

		return options;
	}

	void PrintPreparationFailure(const Aurore::Data::RawDataPreparationFailure& failure) {
		std::cerr << "Raw data preparation failed: " << failure.Message << "\n";
		if (!failure.Path.empty()) 
			std::cerr << "Path: " << failure.Path.string() << "\n";

		if (failure.SystemError)
			std::cerr << "System error: " << failure.SystemError.message() << " (" << failure.SystemError.value() << ")\n";

		if (failure.ExitCode != 0) 
			std::cerr << "Exit code: " << failure.ExitCode << "\n";

		if (!failure.StandardOutput.empty()) 
			std::cerr << "\nstdout:\n" << failure.StandardOutput << "\n";

		if (!failure.StandardError.empty()) 
			std::cerr << "\nstderr:\n" << failure.StandardError << "\n";
	}

	struct CorpusInventory {
		std::uintmax_t FileCount{ 0 };
		std::uintmax_t JsonFileCount{ 0 };

		std::vector<std::filesystem::path> ReportFiles;
		std::vector<std::filesystem::path> MinecraftDataEntries;
	};

	[[nodiscard]] CorpusInventory InspectCorpus(const Aurore::Data::RawDataPreparationResult& result) {
		CorpusInventory inventory;
		std::error_code error;
		auto iterator = std::filesystem::recursive_directory_iterator(result.GenerationRoot, std::filesystem::directory_options::skip_permission_denied, error);
		const auto end = std::filesystem::recursive_directory_iterator{};
		while (!error && iterator != end) {
			std::error_code entry_error;
			if (iterator->is_regular_file(entry_error)) {
				inventory.FileCount++;
				if (iterator->path().extension() == ".json")
					inventory.JsonFileCount++;
			}
			iterator.increment(error);
		}

		error.clear();

		auto report_iterator = std::filesystem::directory_iterator(result.ReportsRoot, error);
		const auto report_end = std::filesystem::directory_iterator{};
		while (!error && report_iterator != report_end) {
			if (report_iterator->is_regular_file()) 
				inventory.ReportFiles.push_back(report_iterator->path().filename());
			report_iterator.increment(error);
		}

		error.clear();

		const auto minecraft_root = result.DataRoot / "minecraft";
		auto data_iterator = std::filesystem::directory_iterator(minecraft_root, error);
		const auto data_end = std::filesystem::directory_iterator{};
		while (!error && data_iterator != data_end) {
			inventory.MinecraftDataEntries.push_back(data_iterator->path().filename());
			data_iterator.increment(error);
		}

		std::ranges::sort(inventory.ReportFiles);
		std::ranges::sort(inventory.MinecraftDataEntries);

		return inventory;
	}

	void PrintCorpusSummary(const Aurore::Data::RawDataPreparationResult& result, bool list_files) {
		const auto inventory = InspectCorpus(result);
		std::cout
			<< "\nGenerated corpus\n"
			<< "  Root: " << result.GenerationRoot.string() << "\n"
			<< "  Reports: " << result.ReportsRoot.string() << "\n"
			<< "  Data: " << result.DataRoot.string() << "\n"
			<< "  Files: " << inventory.FileCount << "\n"
			<< "  JSON files: " << inventory.JsonFileCount << "\n";

		if (!inventory.ReportFiles.empty()) {
			std::cout << "\nReport files:\n";
			for (const auto& file : inventory.ReportFiles) 
				std::cout << "  " << file.string() << "\n";
		}

		if (!inventory.MinecraftDataEntries.empty()) {
			std::cout << "\ndata/minecraft top-level entries:\n";
			for (const auto& entry : inventory.MinecraftDataEntries)
				std::cout << "  " << entry.string() << "\n";
		}

		if (!list_files) return;

		std::cout << "\nAll generated files:\n";

		std::error_code error;
		auto iterator = std::filesystem::recursive_directory_iterator(result.GenerationRoot, std::filesystem::directory_options::skip_permission_denied, error);
		const auto end = std::filesystem::recursive_directory_iterator{};
		while (!error && iterator != end) {
			std::error_code entry_error;
			if (iterator->is_regular_file(entry_error)) 
				std::cout << "  " << std::filesystem::relative(iterator->path(), result.GenerationRoot, entry_error).string() << "\n";
			iterator.increment(error);
		}
	}

	int RunPrepareRaw(const CommandLineOptions& options) {
		auto request = Aurore::Data::DataPreparationRequest::Create(
			Aurore::Data::DataPreparationOptions{
				.Version = Aurore::Data::MinecraftVersion{
					.Id = options.MinecraftVersion,
					.ProtocolVersion = options.ProtocolVersion,
				},
				.Source = Aurore::Data::LocalJarSource{
					.JarPath = options.JarPath,
				},
				.CacheRoot = options.OutputRoot,
			}
		);

		if (!request) {
			std::cerr << "Invalid data preparation request.\n";
			return 2;
		}

		const auto result = Aurore::Data::DataPreparationService::PrepareRaw(*request, Aurore::Data::DataPreparationRuntimeOptions{ .JavaExecutable = options.JavaExecutable });
		if (!result) {
			PrintPreparationFailure(result.error());
			return 1;
		}

		std::cout
			<< "Raw Minecraft data generated successfully.\n\n"
			<< "Minecraft version: " << result->Version.Id << "\n"
			<< "Protocol version: " << result->Version.ProtocolVersion << "\n"
			<< "Source JAR: " << result->SourceJarPath.string() << "\n"
			<< "Source SHA-1: " << result->SourceSha1 << "\n"
			<< "Java: " << result->JavaVersionText << " (major " << result->JavaMajorVersion << ")\n"
			<< "Java executable: " << result->JavaExecutable.string() << "\n";

		PrintCorpusSummary(*result, options.ListFiles);
		return 0;
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		PrintUsage();
		return 2;
	}

	const std::string_view command{ argv[1] };
	if (command == "--help" || command == "-h" || command == "help") {
		PrintUsage();
		return 0;
	}

	if (command == "prepare-raw") {
		const auto options = ParsePrepareRawOptions(argc, argv);
		if (!options) return 2;
		return RunPrepareRaw(*options);
	}

	std::cerr << "Unknown command: " << command << "\n";
	PrintUsage();
	return 2;
}
