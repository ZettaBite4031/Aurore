#pragma once

#include <Aurore/Data/MinecraftDataCorpus.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace Aurore::Data {
	enum class VanillaDataImportErrorCode : std::uint8_t {
		UnsupportedMinecraftVersion,
		UnsupportedProtocolVersion,
		InvalidGeneration,
		SnapshotBuildFailed,
	};

	struct VanillaDataImportError final {
		VanillaDataImportErrorCode Code;
		std::string MinecraftVersion;
		std::int32_t ProtocolVersion{ 0 };
		Aurore::Util::RegistryGeneration Generation{ Aurore::Util::NoRegistryGeneration };
		std::optional<Aurore::Util::RegistrySnapshotError> SnapshotError;
	};

	class VanillaDataImporter final {
	public:
		VanillaDataImporter() = delete;

		[[nodiscard]] static std::expected<std::shared_ptr<const Aurore::Util::RegistrySnapshot>, VanillaDataImportError> Import(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation);
	};
}
