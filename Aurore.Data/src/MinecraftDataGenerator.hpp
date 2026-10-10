#pragma once

#include "JavaRuntime.hpp"
#include "Process.hpp"

#include <Aurore/Data/MinecraftSource.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace Aurore::Data::Detail {
	struct MinecraftGeneratedData final {
		MinecraftVersion Version;

		std::filesystem::path Root;
		std::filesystem::path ReportsRoot;
		std::filesystem::path DataRoot;

		std::string SourceSha1;
	};

	enum class MinecraftDataGeneratorErrorCode : std::uint8_t {
		InvalidSourceArtifact,
		InvalidJavaRuntime,
		IncompatibleJavaVersion,

		StagingPathResolutionFailed,
		StagingCleanupFailed,
		StagingDirectoryCreationFailed,

		ProcessFailed,
		GeneratorFailed,

		OutputInspectionFailed,
		RegistryReportMissing,
		ServerDataMissing,
	};

	struct MinecraftDataGeneratorError final {
		MinecraftDataGeneratorErrorCode Code;

		std::filesystem::path Path;
		std::error_code SystemError;

		std::optional<ProcessError> ProcessFailure;

		int ExitCode{ 0 };

		std::string StdOut;
		std::string StdErr;
	};

	class MinecraftDataGenerator final {
	public:
		[[nodiscard]] static std::expected<MinecraftGeneratedData, MinecraftDataGeneratorError> Generate(const MinecraftSourceArtifact& source, const JavaRuntime& java, const std::filesystem::path& staging_root);
	};
}
