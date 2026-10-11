#pragma once

#include <Aurore/Data/DataPreparation.hpp>
#include <Aurore/Data/MinecraftSource.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace Aurore::Data {
	struct DataPreparationRuntimeOptions final {
		std::optional<std::filesystem::path> JavaExecutable;
	};

	struct RawDataPreparationResult final {
		MinecraftVersion Version;

		MinecraftSourceKind SourceKind{ MinecraftSourceKind::LocalJar };
		std::filesystem::path SourceJarPath;
		std::uintmax_t SourceFileSize{ 0 };
		std::string SourceSha1;

		std::optional<std::uint32_t> RequiredJavaMajorVersion;
		bool OfficiallyVerified{ false };

		std::filesystem::path JavaExecutable;
		std::uint32_t JavaMajorVersion{ 0 };
		std::string JavaVersionText;

		std::filesystem::path GenerationRoot;
		std::filesystem::path ReportsRoot;
		std::filesystem::path DataRoot;
	};

	enum class RawDataPreparationErrorCode : std::uint8_t {
		SourceResolutionFailed,
		JavaResolutionFailed,
		DataGenerationFailed,
	};

	struct RawDataPreparationFailure final {
		RawDataPreparationErrorCode Code;

		std::string Message;
		std::filesystem::path Path;
		std::error_code SystemError;

		int ExitCode{ 0 };

		std::string StandardOutput;
		std::string StandardError;
	};

	class DataPreparationService final {
	public:
		[[nodiscard]] static std::expected<RawDataPreparationResult, RawDataPreparationFailure> PrepareRaw(const DataPreparationRequest& request, const DataPreparationRuntimeOptions& runtime = {});
	};
}
