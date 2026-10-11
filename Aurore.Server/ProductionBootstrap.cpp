#include "ProductionBootstrap.hpp"

#include <Aurore/Data/DataPreparation.hpp>
#include <Aurore/Data/DataPreparationService.hpp>
#include <Aurore/Data/MinecraftDataCorpus.hpp>
#include <Aurore/Data/VanillaDataImporter.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::DataPreparationOptions;
	using Aurore::Data::DataPreparationRequest;
	using Aurore::Data::DataPreparationRuntimeOptions;
	using Aurore::Data::MinecraftDataCorpusError;
	using Aurore::Data::MinecraftDataCorpusErrorCode;
	using Aurore::Data::MinecraftDataCorpusFactory;
	using Aurore::Data::MinecraftVersion;
	using Aurore::Data::OfficialDownloadSource;
	using Aurore::Data::RawDataPreparationFailure;
	using Aurore::Data::RawDataPreparationResult;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImporter;
	using Aurore::Server::ProductionBootstrapError;
	using Aurore::Server::ProductionBootstrapErrorCode;
	using Aurore::Server::ProductionBootstrapOptions;

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;

		return std::ranges::all_of(value, [](char character) {
			return std::isxdigit(static_cast<unsigned char>(character)) != 0;
		});
	}

	[[nodiscard]] std::string DescribeCorpusError(MinecraftDataCorpusErrorCode code) {
		switch (code) {
		case MinecraftDataCorpusErrorCode::InvalidVersion: return "the generated corpus has an invalid Minecraft version";
		case MinecraftDataCorpusErrorCode::InvalidProtocolVersion: return "the generated corpus has an invalid protocol version";
		case MinecraftDataCorpusErrorCode::InvalidSourceIdentity: return "the generated corpus has an invalid source identity";
		case MinecraftDataCorpusErrorCode::PathInspectionFailed: return "the generated corpus could not be inspected";
		case MinecraftDataCorpusErrorCode::MissingRegistryReport: return "the generated corpus is missing reports/registries.json";
		case MinecraftDataCorpusErrorCode::MissingMinecraftData: return "the generated corpus is missing data/minecraft";
		}
		return "the generated Minecraft corpus is invalid";
	}

	[[nodiscard]] std::string DescribeImportError(const VanillaDataImportError& error) {
		std::string result = std::format("vanilla data import failed at stage {} with error {}", static_cast<unsigned int>(error.Stage), static_cast<unsigned int>(error.Code));
		if (error.Key) result += std::format(", key {}", error.Key->ToString());
		if (!error.Field.empty()) result += std::format(", field {}", error.Field);
		if (!error.Detail.empty()) result += std::format(": {}", error.Detail);
		if (error.SystemError) result += std::format(": {} ({})", error.SystemError.message(), error.SystemError.value());
		return result;
	}

	[[nodiscard]] ProductionBootstrapError TranslatePreparationFailure(const RawDataPreparationFailure& failure) {
		std::string detail;
		if (failure.SystemError)
			detail = std::format("{} ({})", failure.SystemError.message(), failure.SystemError.value());

		if (!failure.StandardError.empty()) {
			if (!detail.empty()) detail += "\n";
			detail += failure.StandardError;
		}
		else if (!failure.StandardOutput.empty()) {
			if (!detail.empty()) detail += "\n";
			detail += failure.StandardOutput;
		}

		return ProductionBootstrapError{
			.Code = ProductionBootstrapErrorCode::DataPreparationFailed,
			.Message = failure.Message.empty()
				? "Minecraft data preparation failed"
				: failure.Message,
			.Path = failure.Path,
			.Detail = std::move(detail),
		};
	}

	[[nodiscard]] ProductionBootstrapError TranslateCorpusFailure(const MinecraftDataCorpusError& error) {
		std::string detail;
		if (error.SystemError)
			detail = std::format("{} ({})", error.SystemError.message(), error.SystemError.value());

		return ProductionBootstrapError{
			.Code = ProductionBootstrapErrorCode::CorpusConstructionFailed,
			.Message = DescribeCorpusError(error.Code),
			.Path = error.Path,
			.Detail = std::move(detail),
		};
	}

	[[nodiscard]] ProductionBootstrapError TranslateImportFailure(const VanillaDataImportError& error) {
		return ProductionBootstrapError{
			.Code = ProductionBootstrapErrorCode::SnapshotImportFailed,
			.Message = DescribeImportError(error),
			.Path = error.Path,
		};
	}

	[[nodiscard]] std::expected<RawDataPreparationResult, ProductionBootstrapError> BuildExistingPreparation(const std::filesystem::path& requested_root) {
		if (requested_root.empty())
			return std::unexpected(ProductionBootstrapError{
				.Code = ProductionBootstrapErrorCode::InvalidExistingCorpusRoot,
				.Message = "AURORE_MINECRAFT_DATA_ROOT is empty",
			});

		std::error_code filesystem_error;
		auto root = std::filesystem::absolute(requested_root, filesystem_error);
		if (filesystem_error)
			return std::unexpected(ProductionBootstrapError{
				.Code = ProductionBootstrapErrorCode::InvalidExistingCorpusRoot,
				.Message = "failed to resolve the configured Minecraft data root",
				.Path = requested_root,
				.Detail = std::format(
					"{} ({})",
					filesystem_error.message(),
					filesystem_error.value()),
				});

		root = root.lexically_normal();
		if (root.filename().empty())
			root = root.parent_path();

		const auto source_sha1 = root.filename().string();
		if (!IsSha1(source_sha1))
			return std::unexpected(ProductionBootstrapError{
				.Code = ProductionBootstrapErrorCode::InvalidExistingCorpusRoot,
				.Message = "the configured Minecraft data root must be the generated SHA-1 directory",
				.Path = root,
				.Detail = "expected a generation root containing reports/ and data/ whose directory name is the 40-character source SHA-1",
			});

		return RawDataPreparationResult{
			.Version = MinecraftVersion{
				.Id = std::string{ Aurore::Server::MinecraftVersion },
				.ProtocolVersion = Aurore::Server::MinecraftProtocolVersion,
			},
			.SourceSha1 = source_sha1,
			.GenerationRoot = root,
			.ReportsRoot = root / "reports",
			.DataRoot = root / "data",
		};
	}

	[[nodiscard]] std::expected<RawDataPreparationResult, ProductionBootstrapError> PrepareMinecraftData(const ProductionBootstrapOptions& options) {
		if (options.ExistingCorpusRoot) return BuildExistingPreparation(*options.ExistingCorpusRoot);

		auto request = DataPreparationRequest::Create(
			DataPreparationOptions{
				.Version = MinecraftVersion{
					.Id = std::string{ Aurore::Server::MinecraftVersion },
					.ProtocolVersion = Aurore::Server::MinecraftProtocolVersion,
				},
				.Source = OfficialDownloadSource{},
				.CacheRoot = options.CacheRoot,
			});

		if (!request)
			return std::unexpected(ProductionBootstrapError{
				.Code = ProductionBootstrapErrorCode::InvalidPreparationRequest,
				.Message = std::format("failed to construct the Minecraft {} data-preparation request", Aurore::Server::MinecraftVersion),
			});

		auto preparation = Aurore::Data::DataPreparationService::PrepareRaw(*request, DataPreparationRuntimeOptions{ .JavaExecutable = options.JavaExecutable, });
		if (!preparation)
			return std::unexpected(TranslatePreparationFailure(preparation.error()));
		return std::move(*preparation);
	}
}

namespace Aurore::Server {
	std::expected<ProductionBootstrapResult, ProductionBootstrapError> ProductionBootstrap::Build(const ProductionBootstrapOptions& options) {
		auto preparation = PrepareMinecraftData(options);
		if (!preparation) return std::unexpected(preparation.error());

		auto corpus = MinecraftDataCorpusFactory::FromPreparation(*preparation);
		if (!corpus) return std::unexpected(TranslateCorpusFailure(corpus.error()));

		auto snapshot = VanillaDataImporter::Import(*corpus, InitialRegistryGeneration);
		if (!snapshot) return std::unexpected(TranslateImportFailure(snapshot.error()));

		return ProductionBootstrapResult{
			.Bootstrap = Aurore::Core::ServerBootstrap{
				.InitialRegistrySnapshot = std::move(*snapshot),
			},
			.CorpusRoot = corpus->Root,
			.ReusedExistingCorpus = options.ExistingCorpusRoot.has_value(),
		};
	}
}
