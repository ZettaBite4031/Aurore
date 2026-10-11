#pragma once

#include <Aurore/Data/VanillaDataImporter.hpp>

#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>

#include <sonnet/sonnet.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Aurore::Data::Detail {
	inline constexpr std::uintmax_t MaximumVanillaRegistryEntryBytes{ 4ull * 1024ull * 1024ull };

	struct VanillaRegistrySourceEntry final {
		Aurore::Util::ResourceLocation Key;
		std::filesystem::path Path;
	};

	struct VanillaRegistryEntryContext final {
		const MinecraftDataCorpus& Corpus;
		Aurore::Util::RegistryGeneration Generation;
		VanillaDataImportStage Stage;
		const VanillaRegistrySourceEntry& Entry;
	};

	[[nodiscard]] VanillaDataImportError MakeImportError(VanillaDataImportErrorCode code, const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, VanillaDataImportStage stage);

	[[nodiscard]] VanillaDataImportError MakeEntryError(VanillaDataImportErrorCode code, const VanillaRegistryEntryContext& context, std::string field = {}, std::string detail = {});

	[[nodiscard]] std::expected<std::vector<VanillaRegistrySourceEntry>, VanillaDataImportError> DiscoverRegistryEntries(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, VanillaDataImportStage stage, const std::filesystem::path& relative_root);

	[[nodiscard]] std::expected<Sonnet::value, VanillaDataImportError> ReadRegistryJson(const VanillaRegistryEntryContext& context);

	[[nodiscard]] const Sonnet::value* FindField(const Sonnet::value& object, std::string_view field) noexcept;

	[[nodiscard]] std::expected<const Sonnet::value*, VanillaDataImportError> ReadRequiredObject(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<bool, VanillaDataImportError> ReadRequiredBool(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<std::optional<bool>, VanillaDataImportError> ReadOptionalBool(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<std::int32_t, VanillaDataImportError> ReadRequiredInt(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context, std::int32_t minimum, std::int32_t maximum);

	[[nodiscard]] std::expected<float, VanillaDataImportError> ReadRequiredFloat(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<double, VanillaDataImportError> ReadRequiredDouble(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<std::string, VanillaDataImportError> ReadRequiredString(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<std::optional<std::string>, VanillaDataImportError> ReadOptionalString(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<Aurore::Util::NbtString, VanillaDataImportError> MakeNbtString(std::string_view value, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<void, VanillaDataImportError> ValidateObjectFields(const Sonnet::value& object, std::span<const std::string_view> allowed_fields, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<Aurore::Util::NbtValue, VanillaDataImportError> ConvertJsonToNbt(const Sonnet::value& value, std::string_view field, const VanillaRegistryEntryContext& context);

	[[nodiscard]] std::expected<void, VanillaDataImportError> ImportDimensionTypes(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, Aurore::Util::RegistrySnapshotBuilder& builder);

	[[nodiscard]] std::expected<void, VanillaDataImportError> ImportBiomes(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, Aurore::Util::RegistrySnapshotBuilder& builder);

	[[nodiscard]] std::expected<void, VanillaDataImportError> ImportNetworkRegistries(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, Aurore::Util::RegistrySnapshotBuilder& builder);
}
