#include "VanillaDataImportSupport.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Data::Detail::VanillaRegistryEntryContext;
	using Aurore::Util::DimensionTypeDefinition;
	using Aurore::Util::NbtCompound;
	using Aurore::Util::NbtString;
	using Aurore::Util::NbtValue;
	using Aurore::Util::RegistrySnapshotBuilder;

	constexpr std::int32_t MinimumDimensionY{ -2032 };
	constexpr std::int32_t MaximumDimensionY{ 2031 };
	constexpr std::int32_t MaximumDimensionHeight{ 4064 };

	constexpr std::array<std::string_view, 3> ValidSkyboxes{ "none", "overworld", "end", };

	constexpr std::array<std::string_view, 2> ValidCardinalLightTypes{ "default", "nether", };

	[[nodiscard]] bool IsAllowed(std::string_view value, std::span<const std::string_view> allowed) noexcept {
		return std::ranges::find(allowed, value) != allowed.end();
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> SetString(NbtCompound& compound, std::u16string_view name, std::string_view value, std::string_view field, const VanillaRegistryEntryContext& context) {
		auto text = Aurore::Data::Detail::MakeNbtString(value, field, context);
		if (!text) return std::unexpected(text.error());
		compound.Set(NbtString{ std::u16string{ name } }, NbtValue::String(std::move(*text)));
		return {};
	}

	[[nodiscard]] std::expected<NbtValue, VanillaDataImportError> TranslateMonsterSpawnLightLevel(const Sonnet::value& root, const VanillaRegistryEntryContext& context) {
		constexpr std::string_view Field{ "monster_spawn_light_level" };
		const auto* value = Aurore::Data::Detail::FindField(root, Field);
		if (value == nullptr)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ Field }));

		if (value->is_number()) {
			auto level = Aurore::Data::Detail::ReadRequiredInt(root, Field, context, 0, 15);
			if (!level) return std::unexpected(level.error());
			return NbtValue::Int(*level);
		}

		if (!value->is_object())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ Field },
				"expected an integer or IntProvider object"));

		auto type = Aurore::Data::Detail::ReadRequiredString(*value, "type", context);
		if (!type) return std::unexpected(type.error());
		if (*type != "minecraft:uniform")
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ Field },
				"only minecraft:uniform is supported for non-constant vanilla dimension light providers"));

		auto minimum = Aurore::Data::Detail::ReadRequiredInt(*value, "min_inclusive", context, 0, 15);
		if (!minimum) return std::unexpected(minimum.error());

		auto maximum = Aurore::Data::Detail::ReadRequiredInt(*value, "max_inclusive", context, 0, 15);
		if (!maximum) return std::unexpected(maximum.error());

		if (*maximum < *minimum)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ Field },
				"uniform provider maximum must be greater than or equal to minimum"));

		NbtCompound provider;
		const auto type_result = SetString(provider, u"type", *type, Field, context);
		if (!type_result) return std::unexpected(type_result.error());
		provider.Set(NbtString{ u"min_inclusive" }, NbtValue::Int(*minimum));
		provider.Set(NbtString{ u"max_inclusive" }, NbtValue::Int(*maximum));
		return NbtValue::Compound(std::move(provider));
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> TranslateDimensionType(const Sonnet::value& root, const VanillaRegistryEntryContext& context) {
		auto has_fixed_time = Aurore::Data::Detail::ReadOptionalBool(root, "has_fixed_time", context);
		if (!has_fixed_time) return std::unexpected(has_fixed_time.error());

		auto has_skylight = Aurore::Data::Detail::ReadRequiredBool(root, "has_skylight", context);
		if (!has_skylight) return std::unexpected(has_skylight.error());

		auto has_ceiling = Aurore::Data::Detail::ReadRequiredBool(root, "has_ceiling", context);
		if (!has_ceiling) return std::unexpected(has_ceiling.error());

		auto coordinate_scale = Aurore::Data::Detail::ReadRequiredDouble(root, "coordinate_scale", context);
		if (!coordinate_scale) return std::unexpected(coordinate_scale.error());
		if (*coordinate_scale < 1.0E-5 || *coordinate_scale > 3.0E7)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "coordinate_scale",
				"coordinate scale must be within the Minecraft 1.21.11 codec range"));

		auto minimum_y = Aurore::Data::Detail::ReadRequiredInt(root, "min_y", context, MinimumDimensionY, MaximumDimensionY);
		if (!minimum_y) return std::unexpected(minimum_y.error());

		auto height = Aurore::Data::Detail::ReadRequiredInt(root, "height", context, 16, MaximumDimensionHeight);
		if (!height) return std::unexpected(height.error());

		auto logical_height = Aurore::Data::Detail::ReadRequiredInt(root, "logical_height", context, 0, MaximumDimensionHeight);
		if (!logical_height) return std::unexpected(logical_height.error());

		if (*minimum_y % 16 != 0)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "min_y",
				"minimum Y must be aligned to 16 blocks"));

		if (*height % 16 != 0)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "height",
				"dimension height must be a multiple of 16"));

		if (static_cast<std::int64_t>(*minimum_y) + *height > static_cast<std::int64_t>(MaximumDimensionY) + 1)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "height",
				"minimum Y plus height exceeds the Minecraft dimension range"));

		if (*logical_height > *height)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "logical_height",
				"logical height cannot exceed physical height"));

		auto infiniburn = Aurore::Data::Detail::ReadRequiredString(root, "infiniburn", context);
		if (!infiniburn) return std::unexpected(infiniburn.error());
		if (!infiniburn->starts_with('#'))
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "infiniburn",
				"infiniburn must be a hashed block tag resource location"));

		const auto infiniburn_key = Aurore::Util::ResourceLocation::Parse(std::string_view{ *infiniburn }.substr(1));
		if (!infiniburn_key)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidResourceLocation, context, "infiniburn",
				"infiniburn contains an invalid block tag resource location"));

		auto ambient_light = Aurore::Data::Detail::ReadRequiredFloat(root, "ambient_light", context);
		if (!ambient_light) return std::unexpected(ambient_light.error());

		auto monster_spawn_light_level = TranslateMonsterSpawnLightLevel(root, context);
		if (!monster_spawn_light_level) return std::unexpected(monster_spawn_light_level.error());

		auto monster_spawn_block_light_limit = Aurore::Data::Detail::ReadRequiredInt(root, "monster_spawn_block_light_limit", context, 0, 15);
		if (!monster_spawn_block_light_limit) return std::unexpected(monster_spawn_block_light_limit.error());

		auto skybox = Aurore::Data::Detail::ReadOptionalString(root, "skybox", context);
		if (!skybox) return std::unexpected(skybox.error());
		if (skybox->has_value() && !IsAllowed(**skybox, ValidSkyboxes))
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "skybox",
				"unknown Minecraft 1.21.11 skybox value"));

		auto cardinal_light = Aurore::Data::Detail::ReadOptionalString(root, "cardinal_light", context);
		if (!cardinal_light) return std::unexpected(cardinal_light.error());
		if (cardinal_light->has_value() && !IsAllowed(**cardinal_light, ValidCardinalLightTypes))
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "cardinal_light",
				"unknown Minecraft 1.21.11 cardinal light value"));

		NbtCompound data;
		if (has_fixed_time->value_or(false))
			data.Set(NbtString{ u"has_fixed_time" }, NbtValue::Byte(1));

		data.Set(NbtString{ u"has_skylight" }, NbtValue::Byte(*has_skylight ? 1 : 0));
		data.Set(NbtString{ u"has_ceiling" }, NbtValue::Byte(*has_ceiling ? 1 : 0));
		data.Set(NbtString{ u"coordinate_scale" }, NbtValue::Double(*coordinate_scale));
		data.Set(NbtString{ u"min_y" }, NbtValue::Int(*minimum_y));
		data.Set(NbtString{ u"height" }, NbtValue::Int(*height));
		data.Set(NbtString{ u"logical_height" }, NbtValue::Int(*logical_height));

		const auto infiniburn_result = SetString(data, u"infiniburn", *infiniburn, "infiniburn", context);
		if (!infiniburn_result) return std::unexpected(infiniburn_result.error());

		data.Set(NbtString{ u"ambient_light" }, NbtValue::Float(*ambient_light));
		data.Set(NbtString{ u"monster_spawn_light_level" }, std::move(*monster_spawn_light_level));
		data.Set(NbtString{ u"monster_spawn_block_light_limit" }, NbtValue::Int(*monster_spawn_block_light_limit));

		if (skybox->has_value()) {
			const auto result = SetString(data, u"skybox", **skybox, "skybox", context);
			if (!result) return std::unexpected(result.error());
		}

		if (cardinal_light->has_value()) {
			const auto result = SetString(data, u"cardinal_light", **cardinal_light, "cardinal_light", context);
			if (!result) return std::unexpected(result.error());
		}

		return data;
	}
}

namespace Aurore::Data::Detail {
	std::expected<void, VanillaDataImportError> ImportDimensionTypes(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		auto entries = DiscoverRegistryEntries(corpus, generation, VanillaDataImportStage::DimensionTypes, "dimension_type");
		if (!entries) return std::unexpected(entries.error());

		for (const auto& entry : *entries) {
			const VanillaRegistryEntryContext context{
				.Corpus = corpus,
				.Generation = generation,
				.Stage = VanillaDataImportStage::DimensionTypes,
				.Entry = entry,
			};

			auto root = ReadRegistryJson(context);
			if (!root) return std::unexpected(root.error());

			auto data = TranslateDimensionType(*root, context);
			if (!data) return std::unexpected(data.error());

			const auto declaration = builder.DimensionTypes().Declare(entry.Key, DimensionTypeDefinition{ .Data = std::move(*data), });
			if (!declaration) {
				auto error = MakeEntryError(VanillaDataImportErrorCode::RegistryDeclarationFailed, context);
				error.DeclarationError = declaration.error();
				return std::unexpected(std::move(error));
			}
		}

		return {};
	}
}
