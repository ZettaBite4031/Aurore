#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {
	[[nodiscard]]
	const char* FindArgument(int argc, char** argv, std::string_view argument) {
		for (int index{ 1 }; index + 1 < argc; ++index) {
			if (std::string_view{ argv[index] } == argument) 
				return argv[index + 1];
		}
		return nullptr;
	}

	[[nodiscard]]
	std::filesystem::path FindJar(int argc, char** argv) {
		const char* value = FindArgument(argc, argv, "-jar");
		if (value == nullptr) return {};
		return std::filesystem::path{ value };
	}

	int RunFakeMinecraftGenerator(int argc, char** argv) {
		const char* output_value = FindArgument(argc, argv, "--output");
		if (output_value == nullptr) {
			std::cerr << "missing --output\n";
			return 70;
		}

		const auto output = std::filesystem::path{ output_value };
		const auto jar = FindJar(argc, argv);
		const auto jar_name = jar.filename().string();
		if (jar_name == "generator-failure.jar") {
			std::cerr << "simulated generator failure\n";
			return 23;
		}

		std::error_code error;
		if (jar_name != "missing-registry-report.jar") {
			std::filesystem::create_directories(output / "reports", error);
			if (error) return 71;
			std::ofstream report{ output / "reports" / "registries.json" };
			if (!report.is_open()) return 72;
			report << "{}";
		}

		if (jar_name != "missing-server-data.jar") {
			std::filesystem::create_directories(output / "data" / "minecraft", error);
			if (error) return 73;
			std::ofstream marker{ output / "data" / "minecraft" / "test.json" };
			if (!marker.is_open()) return 74;
			marker << "{}";
		}

		std::cout << "simulated Minecraft data generation\n";
		return 0;
	}
}

int main(int argc, char** argv) {
	if (argc < 2) return 64;
	const std::string_view mode{ argv[1] };
	if (mode == "capture") {
		std::cout << "stdout-text\n";
		std::cerr << "stderr-text\n";
		return 7;
	}

	if (mode == "arguments") {
		for (int index{ 2 }; index < argc; ++index)
			std::cout << argv[index] << '\n';
		return 0;
	}

	if (mode == "stdout-flood") {
		std::cout << std::string(4096, 'x');
		return 0;
	}

	if (mode == "working-directory") {
		std::ofstream marker{ "process-working-directory.txt" };
		if (!marker.is_open()) return 66;

		marker << "created";
		return 0;
	}

	if (mode == "-version") {
		std::cerr
			<< "openjdk version \"21.0.8\" 2026-07-21\n"
			<< "OpenJDK Runtime Environment\n";

		return 0;
	}

	if (mode == "-DbundlerMainClass=net.minecraft.data.Main") 
		return RunFakeMinecraftGenerator(argc, argv);

	return 65;
}
