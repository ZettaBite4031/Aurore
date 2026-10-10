#include "MinecraftDataGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftSourceArtifact;
	using Aurore::Data::Detail::JavaRuntime;
	using Aurore::Data::Detail::MinecraftDataGeneratorError;
	using Aurore::Data::Detail::MinecraftDataGeneratorErrorCode;
	using Aurore::Data::Detail::Process;
	using Aurore::Data::Detail::ProcessRequest;

	inline constexpr std::uint32_t MinimumJavaMajor{ 21 };

	[[nodiscard]] MinecraftDataGeneratorError MakeError(MinecraftDataGeneratorErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}) {
		return MinecraftDataGeneratorError{ .Code = code, .Path = std::move(path), .SystemError = system_error };
	}

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;
		return std::ranges::all_of(value, [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
	}

	[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path) {
		const auto value = path.u8string();
		return std::string{ reinterpret_cast<const char*>(value.data()), value.size() };
	}
}

namespace Aurore::Data::Detail {
	std::expected<MinecraftGeneratedData, MinecraftDataGeneratorError> MinecraftDataGenerator::Generate(const MinecraftSourceArtifact& source, const JavaRuntime& java, const std::filesystem::path& staging_root) {
		/*
			MinecraftSourceArtifact normally comes from
			MinecraftSourceResolver, but validate the boundary anyway.
			In particular, the digest becomes part of a filesystem
			path below and therefore must never be trusted blindly.
		*/
		if (source.JarPath.empty() || source.RequestedVersion.Id.empty() || !IsSha1(source.Sha1))
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::InvalidSourceArtifact));

		if (java.Executable.empty() || java.MajorVersion == 0)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::InvalidJavaRuntime));

		/*
			Aurore's current compatibility target is Minecraft
			1.21.11, whose toolchain requires the modern Java 21
			runtime generation.

			Keep this compatibility decision here rather than in
			JavaRuntimeResolver. The resolver identifies Java;
			Minecraft policy decides whether that Java is suitable.
		*/
		if (java.MajorVersion < MinimumJavaMajor)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::IncompatibleJavaVersion, java.Executable));

		if (staging_root.empty())
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingPathResolutionFailed));

		std::error_code error;
		auto resolved_staging_root = std::filesystem::absolute(staging_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingPathResolutionFailed, staging_root, error));

		resolved_staging_root = resolved_staging_root.lexically_normal();

		/*
			The source digest gives each exact server artifact its
			own deterministic staging location. Re-running generation
			for the same bytes replaces only that artifact's staging
			tree.
		*/
		const auto generation_root = resolved_staging_root / "minecraft" / source.Sha1;
		std::filesystem::remove_all(generation_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingCleanupFailed, generation_root, error));

		std::filesystem::create_directories(generation_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingDirectoryCreationFailed, generation_root, error));

		const auto process = Process::Run(ProcessRequest{ .Executable = java.Executable, .Arguments = {
				"-DbundlerMainClass=net.minecraft.data.Main",
				"-jar",
				PathToUtf8(source.JarPath),
				"--server",
				"--reports",
				"--output",
				PathToUtf8(generation_root),
			}, .WorkingDirectory = generation_root, .MaximumStandardOutputBytes = 16 * 1024 * 1024, .MaximumStandardErrorBytes = 16 * 1024 * 1024 });

		if (!process) {
			auto result = MakeError(MinecraftDataGeneratorErrorCode::ProcessFailed, java.Executable);
			result.ProcessFailure = process.error();
			return std::unexpected(std::move(result));
		}

		if (process->ExitCode != 0) {
			auto result = MakeError(MinecraftDataGeneratorErrorCode::GeneratorFailed, source.JarPath);
			result.ExitCode = process->ExitCode;
			result.StdOut = process->StdOut;
			result.StdErr = process->StdErr;
			return std::unexpected(std::move(result));
		}

		const auto reports_root = generation_root / "reports";
		const auto registry_report = reports_root / "registries.json";
		const auto data_root = generation_root / "data";
		const auto minecraft_data = data_root / "minecraft";

		error.clear();
		const bool registry_report_exists = std::filesystem::exists(registry_report, error);
		if (error) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::OutputInspectionFailed, registry_report, error));

		if (!registry_report_exists) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::RegistryReportMissing, registry_report));

		error.clear();
		const bool registry_report_regular = std::filesystem::is_regular_file(registry_report, error);
		if (error) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::OutputInspectionFailed, registry_report, error));

		if (!registry_report_regular)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::RegistryReportMissing, registry_report));

		error.clear();
		const bool minecraft_data_exists = std::filesystem::exists(minecraft_data, error);
		if (error) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::OutputInspectionFailed, minecraft_data, error));

		if (!minecraft_data_exists) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::ServerDataMissing, minecraft_data));

		error.clear();
		const bool minecraft_data_directory = std::filesystem::is_directory(minecraft_data, error);

		if (error) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::OutputInspectionFailed, minecraft_data, error));

		if (!minecraft_data_directory) 
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::ServerDataMissing, minecraft_data));

		return MinecraftGeneratedData{
			.Version = source.RequestedVersion,
			.Root = generation_root,
			.ReportsRoot = reports_root,
			.DataRoot = data_root,
			.SourceSha1 = source.Sha1,
		};
	}
}
