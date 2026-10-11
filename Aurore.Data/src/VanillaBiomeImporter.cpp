#include "VanillaDataImportSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Data::Detail::VanillaRegistryEntryContext;
	using Aurore::Util::BiomeDefinition;
	using Aurore::Util::NbtCompound;
	using Aurore::Util::NbtString;
	using Aurore::Util::NbtValue;
	using Aurore::Util::RegistrySnapshotBuilder;

	constexpr std::array<std::string_view, 2> ValidTemperatureModifiers{ "none", "frozen", };

	constexpr std::array<std::string_view, 3> ValidGrassColorModifiers{ "none", "dark_forest", "swamp", };

	[[nodiscard]] bool IsAllowed(std::string_view value, std::span<const std::string_view> allowed) noexcept {
		return std::ranges::find(allowed, value) != allowed.end();
	}

	[[nodiscard]] bool IsHexDigit(char value) noexcept {
		const auto character = static_cast<unsigned char>(value);
		return std::isxdigit(character) != 0;
	}

	[[nodiscard]] std::expected<NbtValue, VanillaDataImportError> TranslateRgbColor(const Sonnet::value& value, std::string_view field, const VanillaRegistryEntryContext& context) {
		if (value.is_string()) {
			const std::string text{ value.as_string() };
			if (text.size() != 7 || text.front() != '#' || !std::ranges::all_of(text.substr(1), IsHexDigit))
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field },
					"RGB color strings must use #RRGGBB format"));

			auto nbt = Aurore::Data::Detail::MakeNbtString(text, field, context);
			if (!nbt) return std::unexpected(nbt.error());
			return NbtValue::String(std::move(*nbt));
		}

		if (value.is_number()) {
			const double number = value.as_number();
			if (!std::isfinite(number)
				|| std::trunc(number) != number
				|| number < 0.0
				|| number > 16'777'215.0)
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field },
					"RGB integer color must be within 0x000000 through 0xFFFFFF"));

			return NbtValue::Int(static_cast<std::int32_t>(number));
		}

		return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field },
			"RGB color must be a #RRGGBB string or integer"));
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> CopyOptionalColor(const Sonnet::value& effects, std::string_view field, std::u16string_view nbt_name, NbtCompound& output, const VanillaRegistryEntryContext& context) {
		const auto* value = Aurore::Data::Detail::FindField(effects, field);
		if (value == nullptr) return {};

		auto color = TranslateRgbColor(*value, field, context);
		if (!color) return std::unexpected(color.error());

		output.Set(NbtString{ std::u16string{ nbt_name } }, std::move(*color));
		return {};
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> TranslateBiome(const Sonnet::value& root, const VanillaRegistryEntryContext& context) {
		auto has_precipitation = Aurore::Data::Detail::ReadRequiredBool(root, "has_precipitation", context);
		if (!has_precipitation) return std::unexpected(has_precipitation.error());

		auto temperature = Aurore::Data::Detail::ReadRequiredFloat(root, "temperature", context);
		if (!temperature) return std::unexpected(temperature.error());

		auto temperature_modifier = Aurore::Data::Detail::ReadOptionalString(root, "temperature_modifier", context);
		if (!temperature_modifier) return std::unexpected(temperature_modifier.error());
		if (temperature_modifier->has_value() && !IsAllowed(**temperature_modifier, ValidTemperatureModifiers))
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "temperature_modifier",
				"unknown Minecraft 1.21.11 temperature modifier"));

		auto downfall = Aurore::Data::Detail::ReadRequiredFloat(root, "downfall", context);
		if (!downfall) return std::unexpected(downfall.error());

		auto effects = Aurore::Data::Detail::ReadRequiredObject(root, "effects", context);
		if (!effects) return std::unexpected(effects.error());

		const auto* water_color_value = Aurore::Data::Detail::FindField(**effects, "water_color");
		if (water_color_value == nullptr)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, "effects.water_color"));

		auto water_color = TranslateRgbColor(*water_color_value, "effects.water_color", context);
		if (!water_color) return std::unexpected(water_color.error());

		NbtCompound network_effects;
		network_effects.Set(NbtString{ u"water_color" }, std::move(*water_color));

		auto optional_color = CopyOptionalColor(**effects, "foliage_color", u"foliage_color", network_effects, context);
		if (!optional_color) return std::unexpected(optional_color.error());

		optional_color = CopyOptionalColor(**effects, "dry_foliage_color", u"dry_foliage_color", network_effects, context);
		if (!optional_color) return std::unexpected(optional_color.error());

		optional_color = CopyOptionalColor(**effects, "grass_color", u"grass_color", network_effects, context);
		if (!optional_color) return std::unexpected(optional_color.error());

		auto grass_color_modifier = Aurore::Data::Detail::ReadOptionalString(**effects, "grass_color_modifier", context);
		if (!grass_color_modifier) return std::unexpected(grass_color_modifier.error());
		if (grass_color_modifier->has_value() && !IsAllowed(**grass_color_modifier, ValidGrassColorModifiers))
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "effects.grass_color_modifier",
				"unknown Minecraft 1.21.11 grass color modifier"));

		if (grass_color_modifier->has_value() && **grass_color_modifier != "none") {
			auto text = Aurore::Data::Detail::MakeNbtString(**grass_color_modifier, "effects.grass_color_modifier", context);
			if (!text) return std::unexpected(text.error());

			network_effects.Set(NbtString{ u"grass_color_modifier" }, NbtValue::String(std::move(*text)));
		}

		NbtCompound data;
		data.Set(NbtString{ u"has_precipitation" }, NbtValue::Byte(*has_precipitation ? 1 : 0));
		data.Set(NbtString{ u"temperature" }, NbtValue::Float(*temperature));

		if (temperature_modifier->has_value() && **temperature_modifier != "none") {
			auto text = Aurore::Data::Detail::MakeNbtString(**temperature_modifier, "temperature_modifier", context);
			if (!text) return std::unexpected(text.error());

			data.Set(NbtString{ u"temperature_modifier" }, NbtValue::String(std::move(*text)));
		}

		data.Set(NbtString{ u"downfall" }, NbtValue::Float(*downfall));
		data.Set(NbtString{ u"effects" }, NbtValue::Compound(std::move(network_effects)));
		return data;
	}
}

namespace Aurore::Data::Detail {
	std::expected<void, VanillaDataImportError> ImportBiomes(const MinecraftDataCorpus& corpus, Aurore::Util::RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		auto entries = DiscoverRegistryEntries(corpus, generation, VanillaDataImportStage::Biomes, std::filesystem::path{ "worldgen" } / "biome");
		if (!entries) return std::unexpected(entries.error());

		for (const auto& entry : *entries) {
			const VanillaRegistryEntryContext context{
				.Corpus = corpus,
				.Generation = generation,
				.Stage = VanillaDataImportStage::Biomes,
				.Entry = entry,
			};

			auto root = ReadRegistryJson(context);
			if (!root) return std::unexpected(root.error());

			auto data = TranslateBiome(*root, context);
			if (!data) return std::unexpected(data.error());

			const auto declaration = builder.Biomes().Declare(entry.Key, BiomeDefinition{ .Data = std::move(*data), });
			if (!declaration) {
				auto error = MakeEntryError(VanillaDataImportErrorCode::RegistryDeclarationFailed, context);
				error.DeclarationError = declaration.error();
				return std::unexpected(std::move(error));
			}
		}

		return {};
	}
}
