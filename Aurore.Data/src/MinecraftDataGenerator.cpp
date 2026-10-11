#include "MinecraftDataGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftSourceArtifact;
	using Aurore::Data::Detail::JavaRuntime;
	using Aurore::Data::Detail::MinecraftDataGeneratorError;
	using Aurore::Data::Detail::MinecraftDataGeneratorErrorCode;
	using Aurore::Data::Detail::MinecraftGeneratedData;
	using Aurore::Data::Detail::Process;
	using Aurore::Data::Detail::ProcessRequest;

	/*
		Local JAR sources do not carry authoritative Mojang metadata.
		Aurore currently targets Minecraft 1.21.11, whose generator
		requires Java 21, so retain that as the local-source fallback.
	*/
	inline constexpr std::uint32_t FallbackRequiredJavaMajor{ 21 };

	[[nodiscard]] MinecraftDataGeneratorError MakeError(MinecraftDataGeneratorErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}) {
		return MinecraftDataGeneratorError{ .Code = code, .Path = std::move(path), .SystemError = system_error };
	}

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;
		return std::ranges::all_of(value, [](char character) {
			return std::isxdigit(static_cast<unsigned char>(character)) != 0;
		});
	}

	[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path) {
		const auto value = path.u8string();
		return std::string{ reinterpret_cast<const char*>(value.data()), value.size() };
	}

	[[nodiscard]] std::expected<MinecraftGeneratedData, MinecraftDataGeneratorError> ValidateGeneration(const MinecraftSourceArtifact& source, const std::filesystem::path& generation_root) {
		const auto reports_root = generation_root / "reports";
		const auto registry_report = reports_root / "registries.json";
		const auto data_root = generation_root / "data";
		const auto minecraft_data = data_root / "minecraft";

		std::error_code error;
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

	[[nodiscard]] bool IsIncompleteGeneration(const MinecraftDataGeneratorError& error) noexcept {
		return error.Code == MinecraftDataGeneratorErrorCode::RegistryReportMissing || error.Code == MinecraftDataGeneratorErrorCode::ServerDataMissing;
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
		if (source.JarPath.empty() || source.RequestedVersion.Id.empty() || !IsSha1(source.Sha1) || (source.RequiredJavaMajorVersion && *source.RequiredJavaMajorVersion == 0))
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::InvalidSourceArtifact));

		if (java.Executable.empty() || java.MajorVersion == 0)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::InvalidJavaRuntime));

		const std::uint32_t required_java_major = source.RequiredJavaMajorVersion.value_or(FallbackRequiredJavaMajor);
		if (java.MajorVersion < required_java_major)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::IncompatibleJavaVersion, java.Executable));

		if (staging_root.empty())
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingPathResolutionFailed));

		std::error_code error;
		auto resolved_staging_root = std::filesystem::absolute(staging_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingPathResolutionFailed, staging_root, error));

		resolved_staging_root = resolved_staging_root.lexically_normal();
		const auto generation_root = resolved_staging_root / "minecraft" / source.Sha1;

		/*
			The generation directory is content-addressed by the
			server JAR SHA-1. If the output contract is already
			satisfied, it is the exact generation this source needs
			and can be reused without launching the Mojang generator.
		*/
		auto existing = ValidateGeneration(source, generation_root);
		if (existing) return existing;

		if (!IsIncompleteGeneration(existing.error()))
			return std::unexpected(existing.error());

		/*
			A missing report or data tree means the generation is
			incomplete. Remove that stale tree before generating a
			fresh replacement.
		*/
		error.clear();
		std::filesystem::remove_all(generation_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingCleanupFailed, generation_root, error));

		error.clear();
		std::filesystem::create_directories(generation_root, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataGeneratorErrorCode::StagingDirectoryCreationFailed, generation_root, error));

		const auto process = Process::Run(
				ProcessRequest{
					.Executable = java.Executable,
					.Arguments = {
						"-DbundlerMainClass=net.minecraft.data.Main",
						"-jar",
						PathToUtf8(source.JarPath),
						"--server",
						"--reports",
						"--output",
						PathToUtf8(generation_root),
					},
					.WorkingDirectory = generation_root,
					.MaximumStandardOutputBytes = 16 * 1024 * 1024,
					.MaximumStandardErrorBytes = 16 * 1024 * 1024,
				});

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

		return ValidateGeneration(source, generation_root);
	}
}

