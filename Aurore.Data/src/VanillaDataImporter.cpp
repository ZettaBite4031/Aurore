#include <Aurore/Data/VanillaDataImporter.hpp>

#include "VanillaDataImportSupport.hpp"

#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftDataCorpus;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistrySnapshotError;

	constexpr std::string_view SupportedMinecraftVersion{ "1.21.11" };
	constexpr std::int32_t SupportedProtocolVersion{ 774 };
}

namespace Aurore::Data {
	std::expected<std::shared_ptr<const Aurore::Util::RegistrySnapshot>, VanillaDataImportError> VanillaDataImporter::Import(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation) {
		if (corpus.Version.Id != SupportedMinecraftVersion)
			return std::unexpected(Detail::MakeImportError(VanillaDataImportErrorCode::UnsupportedMinecraftVersion, corpus, generation, VanillaDataImportStage::TargetValidation));
		if (corpus.Version.ProtocolVersion != SupportedProtocolVersion)
			return std::unexpected(Detail::MakeImportError(VanillaDataImportErrorCode::UnsupportedProtocolVersion, corpus, generation, VanillaDataImportStage::TargetValidation));
		if (generation == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(Detail::MakeImportError(VanillaDataImportErrorCode::InvalidGeneration, corpus, generation, VanillaDataImportStage::TargetValidation));

		Aurore::Util::RegistrySnapshotBuilder builder;

		const auto dimension_types = Detail::ImportDimensionTypes(corpus, generation, builder);
		if (!dimension_types) return std::unexpected(dimension_types.error());

		const auto biomes = Detail::ImportBiomes(corpus, generation, builder);
		if (!biomes) return std::unexpected(biomes.error());

		const auto network_registries = Detail::ImportNetworkRegistries(corpus, generation, builder);
		if (!network_registries) return std::unexpected(network_registries.error());

		auto snapshot = std::move(builder).Build(generation);
		if (!snapshot) {
			auto error = Detail::MakeImportError(VanillaDataImportErrorCode::SnapshotBuildFailed, corpus, generation, VanillaDataImportStage::SnapshotBuild);
			error.SnapshotError = snapshot.error();
			return std::unexpected(std::move(error));
		}

		return std::move(*snapshot);
	}
}
