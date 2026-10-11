#include <Aurore/Data/VanillaDataImporter.hpp>

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

	[[nodiscard]] VanillaDataImportError MakeError(VanillaDataImportErrorCode code, const MinecraftDataCorpus& corpus, RegistryGeneration generation, std::optional<RegistrySnapshotError> snapshot_error = std::nullopt) {
		return VanillaDataImportError{
			.Code = code,
			.MinecraftVersion = corpus.Version.Id,
			.ProtocolVersion = corpus.Version.ProtocolVersion,
			.Generation = generation,
			.SnapshotError = std::move(snapshot_error),
		};
	}
}

namespace Aurore::Data {
	std::expected<std::shared_ptr<const Aurore::Util::RegistrySnapshot>, VanillaDataImportError> VanillaDataImporter::Import(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation) {
		if (corpus.Version.Id != SupportedMinecraftVersion)
			return std::unexpected(MakeError(VanillaDataImportErrorCode::UnsupportedMinecraftVersion, corpus, generation));
		if (corpus.Version.ProtocolVersion != SupportedProtocolVersion)
			return std::unexpected(MakeError(VanillaDataImportErrorCode::UnsupportedProtocolVersion, corpus, generation));
		if (generation == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(MakeError(VanillaDataImportErrorCode::InvalidGeneration, corpus, generation));

		Aurore::Util::RegistrySnapshotBuilder builder;
		auto snapshot = std::move(builder).Build(generation);
		if (!snapshot)
			return std::unexpected(MakeError(VanillaDataImportErrorCode::SnapshotBuildFailed, corpus, generation, snapshot.error()));
		return std::move(*snapshot);
	}
}
