#include "VanillaDataImportSupport.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftDataCorpus;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Data::Detail::VanillaRegistryEntryContext;
	using Aurore::Util::NbtCompound;
	using Aurore::Util::NbtString;
	using Aurore::Util::NbtValue;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistrySnapshotBuilder;
	using Aurore::Util::ResourceLocation;

	enum class NetworkProjection : std::uint8_t {
		ResourceTexture,
		ModelAndTexture,
		WolfAssets,
		WolfSounds,
		Painting,
		DirectCodec,
		Timeline,
	};

	struct NetworkRegistryDescriptor final {
		std::string_view RegistryKey;
		std::string_view SourceRoot;
		NetworkProjection Projection;
		std::span<const std::string_view> ModelValues;
		std::span<const std::string_view> DirectFields;
		bool RequiredNonEmpty{ false };
	};

	struct EnvironmentAttributeDescriptor final {
		std::string_view Key;
		bool Syncable;
	};

	constexpr std::array<std::string_view, 0> NoValues{};
	constexpr std::array<std::string_view, 2> ColdModelValues{ "normal", "cold", };
	constexpr std::array<std::string_view, 3> CowModelValues{ "normal", "cold", "warm", };
	constexpr std::array<std::string_view, 2> ZombieNautilusModelValues{ "normal", "warm", };
	constexpr std::array<std::string_view, 2> ChatTypeFields{ "chat", "narration", };
	constexpr std::array<std::string_view, 3> TrimPatternFields{ "asset_id", "description", "decal", };
	constexpr std::array<std::string_view, 3> TrimMaterialFields{ "asset_name", "override_armor_assets", "description", };
	constexpr std::array<std::string_view, 5> DamageTypeFields{ "message_id", "scaling", "exhaustion", "effects", "death_message_type", };
	constexpr std::array<std::string_view, 2> BannerPatternFields{ "asset_id", "translation_key", };
	constexpr std::array<std::string_view, 11> EnchantmentFields{ "description", "supported_items", "primary_items", "weight", "max_level", "min_cost", "max_cost", "anvil_cost", "slots", "exclusive_set", "effects", };
	constexpr std::array<std::string_view, 4> JukeboxSongFields{ "sound_event", "description", "length_in_seconds", "comparator_output", };
	constexpr std::array<std::string_view, 4> InstrumentFields{ "sound_event", "use_duration", "range", "description", };
	constexpr std::array<std::string_view, 7> TestEnvironmentFields{ "type", "definitions", "setup", "teardown", "rules", "time", "weather", };
	constexpr std::array<std::string_view, 12> TestInstanceFields{ "type", "function", "environment", "structure", "max_ticks", "setup_ticks", "required", "rotation", "manual_only", "max_attempts", "required_successes", "sky_access", };
	constexpr std::array<std::string_view, 16> DialogFields{ "type", "title", "external_title", "can_close_with_escape", "pause", "after_action", "body", "inputs", "action", "yes", "no", "dialogs", "exit_action", "columns", "button_width", "actions", };
	constexpr std::array<std::string_view, 2> TimelineFields{ "period_ticks", "tracks", };

	constexpr std::array EnvironmentAttributes{
		EnvironmentAttributeDescriptor{ "minecraft:visual/fog_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/fog_start_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/fog_end_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sky_fog_end_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/cloud_fog_end_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/water_fog_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/water_fog_start_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/water_fog_end_distance", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sky_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sunrise_sunset_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/cloud_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/cloud_height", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sun_angle", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/moon_angle", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/star_angle", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/moon_phase", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/star_brightness", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sky_light_color", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/sky_light_factor", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/default_dripstone_particle", true },
		EnvironmentAttributeDescriptor{ "minecraft:visual/ambient_particles", true },
		EnvironmentAttributeDescriptor{ "minecraft:audio/background_music", true },
		EnvironmentAttributeDescriptor{ "minecraft:audio/music_volume", true },
		EnvironmentAttributeDescriptor{ "minecraft:audio/ambient_sounds", true },
		EnvironmentAttributeDescriptor{ "minecraft:audio/firefly_bush_sounds", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/sky_light_level", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/can_start_raid", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/water_evaporates", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/bed_rule", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/respawn_anchor_works", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/nether_portal_spawns_piglin", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/fast_lava", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/increased_fire_burnout", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/eyeblossom_open", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/turtle_egg_hatch_chance", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/piglins_zombify", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/snow_golem_melts", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/creaking_active", true },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/surface_slime_spawn_chance", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/cat_waking_up_gift_chance", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/bees_stay_in_hive", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/monsters_burn", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/can_pillager_patrol_spawn", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/villager_activity", false },
		EnvironmentAttributeDescriptor{ "minecraft:gameplay/baby_villager_activity", false },
	};



	constexpr std::array NetworkRegistries{
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:chat_type",
			.SourceRoot = "chat_type",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = ChatTypeFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:trim_pattern",
			.SourceRoot = "trim_pattern",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = TrimPatternFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:trim_material",
			.SourceRoot = "trim_material",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = TrimMaterialFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:wolf_variant",
			.SourceRoot = "wolf_variant",
			.Projection = NetworkProjection::WolfAssets,
			.ModelValues = NoValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:wolf_sound_variant",
			.SourceRoot = "wolf_sound_variant",
			.Projection = NetworkProjection::WolfSounds,
			.ModelValues = NoValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:pig_variant",
			.SourceRoot = "pig_variant",
			.Projection = NetworkProjection::ModelAndTexture,
			.ModelValues = ColdModelValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:frog_variant",
			.SourceRoot = "frog_variant",
			.Projection = NetworkProjection::ResourceTexture,
			.ModelValues = NoValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:cat_variant",
			.SourceRoot = "cat_variant",
			.Projection = NetworkProjection::ResourceTexture,
			.ModelValues = NoValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:cow_variant",
			.SourceRoot = "cow_variant",
			.Projection = NetworkProjection::ModelAndTexture,
			.ModelValues = CowModelValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:chicken_variant",
			.SourceRoot = "chicken_variant",
			.Projection = NetworkProjection::ModelAndTexture,
			.ModelValues = ColdModelValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:zombie_nautilus_variant",
			.SourceRoot = "zombie_nautilus_variant",
			.Projection = NetworkProjection::ModelAndTexture,
			.ModelValues = ZombieNautilusModelValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:painting_variant",
			.SourceRoot = "painting_variant",
			.Projection = NetworkProjection::Painting,
			.ModelValues = NoValues,
			.DirectFields = NoValues,
			.RequiredNonEmpty = true,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:damage_type",
			.SourceRoot = "damage_type",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = DamageTypeFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:banner_pattern",
			.SourceRoot = "banner_pattern",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = BannerPatternFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:enchantment",
			.SourceRoot = "enchantment",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = EnchantmentFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:jukebox_song",
			.SourceRoot = "jukebox_song",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = JukeboxSongFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:instrument",
			.SourceRoot = "instrument",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = InstrumentFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:test_environment",
			.SourceRoot = "test_environment",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = TestEnvironmentFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:test_instance",
			.SourceRoot = "test_instance",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = TestInstanceFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:dialog",
			.SourceRoot = "dialog",
			.Projection = NetworkProjection::DirectCodec,
			.ModelValues = NoValues,
			.DirectFields = DialogFields,
		},
		NetworkRegistryDescriptor{
			.RegistryKey = "minecraft:timeline",
			.SourceRoot = "timeline",
			.Projection = NetworkProjection::Timeline,
			.ModelValues = NoValues,
			.DirectFields = TimelineFields,
		},
	};

	[[nodiscard]] std::expected<std::string, VanillaDataImportError> ReadResourceLocationString(const Sonnet::value& object, std::string_view member, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = Aurore::Data::Detail::FindField(object, member);
		if (value == nullptr)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_string())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError( VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field },
				"expected resource-location string"));

		std::string text{ value->as_string().data(), value->as_string().size() };
		const auto parsed = ResourceLocation::Parse(text);
		if (!parsed)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidResourceLocation, context, std::string{ field },
				"field is not a valid Minecraft resource location"));

		return text;
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> SetString(NbtCompound& output, std::string_view field, std::string_view value, const VanillaRegistryEntryContext& context) {
		auto name = Aurore::Data::Detail::MakeNbtString(field, field, context);
		if (!name) return std::unexpected(name.error());

		auto text = Aurore::Data::Detail::MakeNbtString(value, field, context);
		if (!text) return std::unexpected(text.error());

		output.Set(std::move(*name), NbtValue::String(std::move(*text)));
		return {};
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> SetProjectedField(NbtCompound& output, const Sonnet::value& source, std::string_view field, const VanillaRegistryEntryContext& context, bool required) {
		const auto* value = Aurore::Data::Detail::FindField(source, field);
		if (value == nullptr) {
			if (!required) return {};
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));
		}

		auto name = Aurore::Data::Detail::MakeNbtString(field, field, context);
		if (!name) return std::unexpected(name.error());

		auto converted = Aurore::Data::Detail::ConvertJsonToNbt(*value, field, context);
		if (!converted) return std::unexpected(converted.error());

		output.Set(std::move(*name), std::move(*converted));
		return {};
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> ValidateSpawnConditions(const Sonnet::value& source, const VanillaRegistryEntryContext& context) {
		const auto* conditions = Aurore::Data::Detail::FindField(source, "spawn_conditions");
		if (conditions == nullptr)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, "spawn_conditions"));

		if (!conditions->is_array())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, "spawn_conditions",
				"expected JSON array"));

		return {};
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectResourceTexture(const Sonnet::value& source, const VanillaRegistryEntryContext& context) {
		constexpr std::array<std::string_view, 2> AllowedFields{ "asset_id", "spawn_conditions", };

		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		const auto conditions = ValidateSpawnConditions(source, context);
		if (!conditions) return std::unexpected(conditions.error());

		auto asset = ReadResourceLocationString(source, "asset_id", "asset_id", context);
		if (!asset) return std::unexpected(asset.error());

		NbtCompound output;
		const auto set_asset = SetString(output, "asset_id", *asset, context);
		if (!set_asset) return std::unexpected(set_asset.error());
		return output;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectModelAndTexture(const Sonnet::value& source, const NetworkRegistryDescriptor& descriptor, const VanillaRegistryEntryContext& context) {
		constexpr std::array<std::string_view, 3> AllowedFields{ "model", "asset_id", "spawn_conditions", };

		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		const auto conditions = ValidateSpawnConditions(source, context);
		if (!conditions) return std::unexpected(conditions.error());

		auto asset = ReadResourceLocationString(source, "asset_id", "asset_id", context);
		if (!asset) return std::unexpected(asset.error());

		auto model = Aurore::Data::Detail::ReadOptionalString(source, "model", context);
		if (!model) return std::unexpected(model.error());

		if (model->has_value()) {
			bool known{ false };
			for (const auto allowed : descriptor.ModelValues)
				if (**model == allowed) {
					known = true;
					break;
				}

			if (!known)
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, "model",
					"model is not valid for this Minecraft 1.21.11 variant registry"));
		}

		NbtCompound output;
		if (model->has_value() && **model != "normal") {
			const auto set_model = SetString(output, "model", **model, context);
			if (!set_model) return std::unexpected(set_model.error());
		}

		const auto set_asset = SetString(output, "asset_id", *asset, context);
		if (!set_asset) return std::unexpected(set_asset.error());

		return output;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectWolfAssets(const Sonnet::value& source, const VanillaRegistryEntryContext& context) {
		constexpr std::array<std::string_view, 2> AllowedFields{ "assets", "spawn_conditions", };
		constexpr std::array<std::string_view, 3> AllowedAssetFields{ "wild", "tame", "angry", };

		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		const auto conditions = ValidateSpawnConditions(source, context);
		if (!conditions) return std::unexpected(conditions.error());

		auto assets = Aurore::Data::Detail::ReadRequiredObject(source, "assets", context);
		if (!assets) return std::unexpected(assets.error());

		const auto asset_fields = Aurore::Data::Detail::ValidateObjectFields(**assets, AllowedAssetFields, context);
		if (!asset_fields) return std::unexpected(asset_fields.error());

		auto wild = ReadResourceLocationString(**assets, "wild", "assets.wild", context);
		if (!wild) return std::unexpected(wild.error());

		auto tame = ReadResourceLocationString(**assets, "tame", "assets.tame", context);
		if (!tame) return std::unexpected(tame.error());

		auto angry = ReadResourceLocationString(**assets, "angry", "assets.angry", context);
		if (!angry) return std::unexpected(angry.error());

		NbtCompound asset_output;
		auto result = SetString(asset_output, "wild", *wild, context);
		if (!result) return std::unexpected(result.error());

		result = SetString(asset_output, "tame", *tame, context);
		if (!result) return std::unexpected(result.error());

		result = SetString(asset_output, "angry", *angry, context);
		if (!result) return std::unexpected(result.error());

		NbtCompound output;
		output.Set(NbtString{ u"assets" }, NbtValue::Compound(std::move(asset_output)));
		return output;
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> ValidateSoundReference(const Sonnet::value& value, std::string_view field, const VanillaRegistryEntryContext& context) {
		if (value.is_string()) {
			const auto parsed = ResourceLocation::Parse(std::string_view{ value.as_string().data(), value.as_string().size() });
			if (parsed) return {};

			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidResourceLocation, context, std::string{ field },
				"sound holder is not a valid resource location"));
		}

		if (!value.is_object())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field },
				"sound holder must be a resource location or inline sound object"));

		constexpr std::array<std::string_view, 2> AllowedFields{ "sound_id", "range", };
		const auto allowed = Aurore::Data::Detail::ValidateObjectFields(value, AllowedFields, context);
		if (!allowed) return std::unexpected(allowed.error());

		auto sound_id = ReadResourceLocationString(value, "sound_id", field, context);
		if (!sound_id) return std::unexpected(sound_id.error());

		const auto* range = Aurore::Data::Detail::FindField(value, "range");
		if (range != nullptr && !range->is_number())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field },
				"inline sound range must be numeric"));

		return {};
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectWolfSounds( const Sonnet::value& source, const VanillaRegistryEntryContext& context) {
		constexpr std::array<std::string_view, 6> SoundFields{ "ambient_sound", "death_sound", "growl_sound", "hurt_sound", "pant_sound", "whine_sound", };
		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, SoundFields, context);
		if (!fields) return std::unexpected(fields.error());

		NbtCompound output;
		for (const auto field : SoundFields) {
			const auto* value = Aurore::Data::Detail::FindField(source, field);
			if (value == nullptr)
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

			const auto sound = ValidateSoundReference(*value, field, context);
			if (!sound) return std::unexpected(sound.error());

			const auto projected = SetProjectedField(output, source, field, context, true);
			if (!projected) return std::unexpected(projected.error());
		}

		return output;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectPainting(const Sonnet::value& source, const VanillaRegistryEntryContext& context) {
		constexpr std::array<std::string_view, 5> AllowedFields{ "width", "height", "asset_id", "title", "author", };

		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		auto width = Aurore::Data::Detail::ReadRequiredInt(source, "width", context, 1, 16);
		if (!width) return std::unexpected(width.error());

		auto height = Aurore::Data::Detail::ReadRequiredInt(source, "height", context, 1, 16);
		if (!height) return std::unexpected(height.error());

		auto asset = ReadResourceLocationString(source, "asset_id", "asset_id", context);
		if (!asset) return std::unexpected(asset.error());

		NbtCompound output;
		output.Set(NbtString{ u"width" }, NbtValue::Int(*width));
		output.Set(NbtString{ u"height" }, NbtValue::Int(*height));

		const auto set_asset = SetString(output, "asset_id", *asset, context);
		if (!set_asset) return std::unexpected(set_asset.error());

		auto optional = SetProjectedField(output, source, "title", context, false);
		if (!optional) return std::unexpected(optional.error());

		optional = SetProjectedField(output, source, "author", context, false);
		if (!optional) return std::unexpected(optional.error());

		return output;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectDirectCodec(const Sonnet::value& source, const NetworkRegistryDescriptor& descriptor, const VanillaRegistryEntryContext& context) {
		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, descriptor.DirectFields, context);
		if (!fields) return std::unexpected(fields.error());

		NbtCompound output;
		for (const auto& [name, value] : source.as_object()) {
			const std::string_view field{ name.data(), name.size() };

			auto nbt_name = Aurore::Data::Detail::MakeNbtString(field, field, context);
			if (!nbt_name) return std::unexpected(nbt_name.error());

			auto converted = Aurore::Data::Detail::ConvertJsonToNbt(value, field, context);
			if (!converted) return std::unexpected(converted.error());

			output.Set(std::move(*nbt_name), std::move(*converted));
		}

		return output;
	}

	[[nodiscard]] const EnvironmentAttributeDescriptor* FindEnvironmentAttribute(const ResourceLocation& key) noexcept {
		const auto serialized = key.ToString();
		for (const auto& descriptor : EnvironmentAttributes)
			if (descriptor.Key == serialized)
				return &descriptor;
		return nullptr;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectTimeline(const Sonnet::value& source, const NetworkRegistryDescriptor& descriptor, const VanillaRegistryEntryContext& context) {
		const auto fields = Aurore::Data::Detail::ValidateObjectFields(source, descriptor.DirectFields, context);
		if (!fields) return std::unexpected(fields.error());

		NbtCompound output;
		const auto period = SetProjectedField(output, source, "period_ticks", context, false);
		if (!period) return std::unexpected(period.error());

		const auto* tracks = Aurore::Data::Detail::FindField(source, "tracks");
		if (tracks == nullptr) return output;

		if (!tracks->is_object())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, "tracks",
				"expected JSON object"));

		NbtCompound network_tracks;
		for (const auto& [name, value] : tracks->as_object()) {
			const std::string_view field{ name.data(), name.size() };

			const auto attribute_key = ResourceLocation::Parse(field);
			if (!attribute_key)
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidResourceLocation, context, std::string{ field },
					"timeline track key is not a valid environment attribute resource location"));

			const auto* attribute = FindEnvironmentAttribute(*attribute_key);
			if (attribute == nullptr)
				return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field },
					"timeline references an unknown Minecraft 1.21.11 environment attribute"));

			if (!attribute->Syncable) continue;

			auto nbt_name = Aurore::Data::Detail::MakeNbtString(field, field, context);
			if (!nbt_name) return std::unexpected(nbt_name.error());

			auto converted = Aurore::Data::Detail::ConvertJsonToNbt(value, std::string{ "tracks." }.append(field), context);
			if (!converted) return std::unexpected(converted.error());

			network_tracks.Set(std::move(*nbt_name), std::move(*converted));
		}

		if (!network_tracks.Empty())
			output.Set(NbtString{ u"tracks" }, NbtValue::Compound(std::move(network_tracks)));

		return output;
	}

	[[nodiscard]] std::expected<NbtCompound, VanillaDataImportError> ProjectNetworkEntry(const Sonnet::value& source, const NetworkRegistryDescriptor& descriptor, const VanillaRegistryEntryContext& context) {
		switch (descriptor.Projection) {
		case NetworkProjection::ResourceTexture: return ProjectResourceTexture(source, context);
		case NetworkProjection::ModelAndTexture: return ProjectModelAndTexture(source, descriptor, context);
		case NetworkProjection::WolfAssets: return ProjectWolfAssets(source, context);
		case NetworkProjection::WolfSounds: return ProjectWolfSounds(source, context);
		case NetworkProjection::Painting: return ProjectPainting(source, context);
		case NetworkProjection::DirectCodec: return ProjectDirectCodec(source, descriptor, context);
		case NetworkProjection::Timeline: return ProjectTimeline(source, descriptor, context);
		}
		std::unreachable();
	}
}

namespace Aurore::Data::Detail {
	std::expected<void, VanillaDataImportError> ImportNetworkRegistries(const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		for (const auto& descriptor : NetworkRegistries) {
			auto registry_key = ResourceLocation::Parse(descriptor.RegistryKey);
			if (!registry_key) {
				auto error = MakeImportError(VanillaDataImportErrorCode::InvalidResourceLocation, corpus, generation, VanillaDataImportStage::NetworkRegistries);
				error.Field = std::string{ descriptor.RegistryKey };
				error.Detail = "built-in network registry descriptor contains an invalid key";
				return std::unexpected(std::move(error));
			}

			std::vector<VanillaRegistrySourceEntry> source_entries;
			auto entries = DiscoverRegistryEntries(corpus, generation, VanillaDataImportStage::NetworkRegistries, std::filesystem::path{ descriptor.SourceRoot });
			if (entries) source_entries = std::move(*entries);
			else {
				const auto code = entries.error().Code;
				const bool absent_or_empty = code == VanillaDataImportErrorCode::MissingRegistrySource || code == VanillaDataImportErrorCode::EmptyRegistrySource;
				if (descriptor.RequiredNonEmpty || !absent_or_empty) return std::unexpected(entries.error());
			}

			auto registry = builder.NetworkRegistries().CreateRegistryBuilder(*registry_key);
			for (const auto& entry : source_entries) {
				const VanillaRegistryEntryContext context{
					.Corpus = corpus,
					.Generation = generation,
					.Stage = VanillaDataImportStage::NetworkRegistries,
					.Entry = entry,
				};

				auto root = ReadRegistryJson(context);
				if (!root) return std::unexpected(root.error());

				auto data = ProjectNetworkEntry(*root, descriptor, context);
				if (!data) return std::unexpected(data.error());

				const auto declaration = registry.Declare(entry.Key, Aurore::Util::NetworkRegistryValue{ std::move(*data) });
				if (!declaration) {
					auto error = MakeEntryError(VanillaDataImportErrorCode::RegistryDeclarationFailed, context);
					error.DeclarationError = declaration.error();
					return std::unexpected(std::move(error));
				}
			}

			auto built = std::move(registry).Build();
			if (!built) {
				auto error = MakeImportError(VanillaDataImportErrorCode::NetworkRegistryBuildFailed, corpus, generation, VanillaDataImportStage::NetworkRegistries);
				error.Key = *registry_key;
				error.Field = std::string{ descriptor.RegistryKey };
				error.NetworkRegistryBuildError = built.error();
				return std::unexpected(std::move(error));
			}

			const auto declaration = builder.NetworkRegistries().Declare(std::move(*built));
			if (!declaration) {
				auto error = MakeImportError(VanillaDataImportErrorCode::NetworkRegistryCollectionDeclarationFailed, corpus, generation, VanillaDataImportStage::NetworkRegistries);
				error.Key = *registry_key;
				error.Field = std::string{ descriptor.RegistryKey };
				error.NetworkRegistryCollectionError = declaration.error();
				return std::unexpected(std::move(error));
			}
		}

		return {};
	}
}
