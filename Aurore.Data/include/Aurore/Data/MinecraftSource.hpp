#pragma once

#include <Aurore/Data/DataPreparation.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace Aurore::Data {
	enum class MinecraftSourceKind : std::uint8_t {
		LocalJar,
		OfficialDownload,
	};

	struct MinecraftSourceArtifact final {
		MinecraftVersion RequestedVersion;

		std::filesystem::path JarPath;
		MinecraftSourceKind SourceKind;

		std::uintmax_t FileSize{ 0 };
		std::string Sha1;
	};

	enum class MinecraftSourceErrorCode : std::uint8_t {
		SourceNotImplemented,

		PathResolutionFailed,
		PathInspectionFailed,

		FileNotFound,
		NotRegularFile,
		FileOpenFailed,
		FileReadFailed,
		EmptyFile,
	};

	struct MinecraftSourceError final {
		MinecraftSourceErrorCode Code;

		std::filesystem::path Path;
		std::error_code SystemError;
	};

	class MinecraftSourceResolver final {
	public:
		[[nodiscard]] static std::expected<MinecraftSourceArtifact, MinecraftSourceError> Resolve(const DataPreparationRequest& request);
	};
}
