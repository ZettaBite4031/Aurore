#pragma once

#include <Aurore/Data/MinecraftDataCorpus.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace Aurore::Data {
	enum class VanillaDataImportStage : std::uint8_t {
		TargetValidation,
		StaticRegistries,
		DimensionTypes,
		Biomes,
		NetworkRegistries,
		SnapshotBuild,
	};

	enum class VanillaDataImportErrorCode : std::uint8_t {
		UnsupportedMinecraftVersion,
		UnsupportedProtocolVersion,
		InvalidGeneration,

		MissingRegistrySource,
		EmptyRegistrySource,
		PathInspectionFailed,
		SourceEntryTooLarge,
		SourceReadFailed,

		JsonParseFailed,
		InvalidJsonRoot,
		MissingField,
		UnexpectedField,
		InvalidFieldType,
		InvalidFieldValue,
		InvalidResourceLocation,
		UnsupportedJsonValue,
		HeterogeneousJsonArray,

		RegistryDeclarationFailed,
		NetworkRegistryBuildFailed,
		NetworkRegistryCollectionDeclarationFailed,
		SnapshotBuildFailed,
	};

	struct VanillaDataImportError final {
		VanillaDataImportErrorCode Code;
		VanillaDataImportStage Stage{ VanillaDataImportStage::TargetValidation };

		std::string MinecraftVersion;
		std::int32_t ProtocolVersion{ 0 };
		Aurore::Util::RegistryGeneration Generation{ Aurore::Util::NoRegistryGeneration };

		std::filesystem::path Path;
		std::optional<Aurore::Util::ResourceLocation> Key;
		std::string Field;
		std::string Detail;
		std::error_code SystemError;

		std::optional<Aurore::Util::RegistryDeclarationError> DeclarationError;
		std::optional<Aurore::Util::RegistryBuildError> NetworkRegistryBuildError;
		std::optional<Aurore::Util::NetworkRegistryCollectionDeclarationError> NetworkRegistryCollectionError;
		std::optional<Aurore::Util::RegistrySnapshotError> SnapshotError;
	};

	class VanillaDataImporter final {
	public:
		VanillaDataImporter() = delete;

		[[nodiscard]] static std::expected<std::shared_ptr<const Aurore::Util::RegistrySnapshot>, VanillaDataImportError> Import(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation);
	};
}
