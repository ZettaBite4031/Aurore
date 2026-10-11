#pragma once

#include <Aurore/Data/DataPreparationService.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace Aurore::Data {
	struct MinecraftDataCorpus final {
		MinecraftVersion Version;
		std::string SourceSha1;
		std::filesystem::path Root;
		std::filesystem::path ReportsRoot;
		std::filesystem::path RegistryReport;
		std::filesystem::path DataRoot;
		std::filesystem::path MinecraftDataRoot;
	};

	enum class MinecraftDataCorpusErrorCode : std::uint8_t {
		InvalidVersion,
		InvalidProtocolVersion,
		InvalidSourceIdentity,
		PathInspectionFailed,
		MissingRegistryReport,
		MissingMinecraftData,
	};

	struct MinecraftDataCorpusError final {
		MinecraftDataCorpusErrorCode Code;
		std::filesystem::path Path;
		std::error_code SystemError;
	};

	class MinecraftDataCorpusFactory final {
	public:
		MinecraftDataCorpusFactory() = delete;

		[[nodiscard]] static std::expected<MinecraftDataCorpus, MinecraftDataCorpusError> FromPreparation(const RawDataPreparationResult& preparation);
	};
}
