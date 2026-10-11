#include <Aurore/Data/MinecraftDataCorpus.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftDataCorpusError;
	using Aurore::Data::MinecraftDataCorpusErrorCode;

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;
		return std::ranges::all_of(value, [](char character) {
			return std::isxdigit(static_cast<unsigned char>(character)) != 0;
		});
	}

	[[nodiscard]] MinecraftDataCorpusError MakeError(
		MinecraftDataCorpusErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}) {

		return MinecraftDataCorpusError{
			.Code = code,
			.Path = std::move(path),
			.SystemError = system_error,
		};
	}
}

namespace Aurore::Data {
	std::expected<MinecraftDataCorpus, MinecraftDataCorpusError> MinecraftDataCorpusFactory::FromPreparation(
		const RawDataPreparationResult& preparation) {

		if (preparation.Version.Id.empty())
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::InvalidVersion));
		if (preparation.Version.ProtocolVersion <= 0)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::InvalidProtocolVersion));
		if (!IsSha1(preparation.SourceSha1))
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::InvalidSourceIdentity));

		const auto registry_report = preparation.ReportsRoot / "registries.json";
		const auto minecraft_data = preparation.DataRoot / "minecraft";
		std::error_code error;

		const bool registry_report_exists = std::filesystem::exists(registry_report, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::PathInspectionFailed, registry_report, error));
		if (!registry_report_exists)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::MissingRegistryReport, registry_report));

		error.clear();
		const bool registry_report_is_file = std::filesystem::is_regular_file(registry_report, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::PathInspectionFailed, registry_report, error));
		if (!registry_report_is_file)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::MissingRegistryReport, registry_report));

		error.clear();
		const bool minecraft_data_exists = std::filesystem::exists(minecraft_data, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::PathInspectionFailed, minecraft_data, error));
		if (!minecraft_data_exists)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::MissingMinecraftData, minecraft_data));

		error.clear();
		const bool minecraft_data_is_directory = std::filesystem::is_directory(minecraft_data, error);
		if (error)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::PathInspectionFailed, minecraft_data, error));
		if (!minecraft_data_is_directory)
			return std::unexpected(MakeError(MinecraftDataCorpusErrorCode::MissingMinecraftData, minecraft_data));

		return MinecraftDataCorpus{
			.Version = preparation.Version,
			.SourceSha1 = preparation.SourceSha1,
			.Root = preparation.GenerationRoot,
			.ReportsRoot = preparation.ReportsRoot,
			.RegistryReport = registry_report,
			.DataRoot = preparation.DataRoot,
			.MinecraftDataRoot = minecraft_data,
		};
	}
}
