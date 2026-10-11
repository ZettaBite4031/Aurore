#include <Aurore/Data/MinecraftDataCorpus.hpp>
#include <Aurore/Data/VanillaDataImporter.hpp>

#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/Registry.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::MinecraftDataCorpus;
		using Aurore::Data::MinecraftDataCorpusFactory;
		using Aurore::Data::RawDataPreparationResult;
		using Aurore::Data::VanillaDataImporter;
		using Aurore::Data::VanillaDataImportErrorCode;
		using Aurore::Data::VanillaDataImportStage;
		using Aurore::Util::NbtCompound;
		using Aurore::Util::RegistryGeneration;
		using Aurore::Util::ResourceLocation;

		[[nodiscard]] ResourceLocation ParseRequired(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			EXPECT_TRUE(result.has_value());
			return std::move(result.value());
		}

		[[nodiscard]] std::string ReadRequiredText(const NbtCompound& compound, std::u16string_view name) {
			const auto* value = compound.Find(name);
			EXPECT_NE(value, nullptr);
			if (value == nullptr) return {};

			const auto* text = value->AsString();
			EXPECT_NE(text, nullptr);
			if (text == nullptr) return {};

			auto utf8 = text->ToUtf8();
			EXPECT_TRUE(utf8.has_value());
			return utf8.value_or(std::string{});
		}

		class VanillaDataImporterTests : public ::testing::Test {
		protected:
			void SetUp() override {
				m_Root = std::filesystem::temp_directory_path() / "aurore-vanilla-data-importer-tests";

				std::error_code error;
				std::filesystem::remove_all(m_Root, error);

				error.clear();
				std::filesystem::create_directories(m_Root / "reports", error);
				ASSERT_FALSE(error);

				std::filesystem::create_directories(m_Root / "data" / "minecraft" / "dimension_type", error);
				ASSERT_FALSE(error);

				std::filesystem::create_directories(m_Root / "data" / "minecraft" / "worldgen" / "biome", error);
				ASSERT_FALSE(error);

				WriteFile("reports/registries.json", "{}");
				WriteDimensionFixtures();
				WriteBiomeFixtures();
				WriteNetworkFixtures();
			}

			void TearDown() override {
				std::error_code error;
				std::filesystem::remove_all(m_Root, error);
			}

			void WriteFile(const std::filesystem::path& relative, std::string_view content) const {
				const auto path = m_Root / relative;

				std::error_code error;
				std::filesystem::create_directories(path.parent_path(), error);
				ASSERT_FALSE(error);

				std::ofstream stream{ path, std::ios::binary };
				ASSERT_TRUE(stream.is_open());
				stream.write(content.data(), static_cast<std::streamsize>(content.size()));
				ASSERT_TRUE(stream.good());
			}

			void WriteDimensionFixtures() const {
				WriteFile(
					"data/minecraft/dimension_type/overworld.json",
					R"json({
						"has_skylight": true,
						"has_ceiling": false,
						"coordinate_scale": 1.0,
						"min_y": -64,
						"height": 384,
						"logical_height": 384,
						"infiniburn": "#minecraft:infiniburn_overworld",
						"ambient_light": 0.0,
						"monster_spawn_light_level": {
							"type": "minecraft:uniform",
							"min_inclusive": 0,
							"max_inclusive": 7
						},
						"monster_spawn_block_light_limit": 0,
						"skybox": "overworld",
						"attributes": {
							"minecraft:visual/sky_color": "#78a7ff"
						},
						"timelines": "#minecraft:in_overworld"
					})json");

				WriteFile(
					"data/minecraft/dimension_type/the_nether.json",
					R"json({
						"has_fixed_time": true,
						"has_skylight": false,
						"has_ceiling": true,
						"coordinate_scale": 8.0,
						"min_y": 0,
						"height": 256,
						"logical_height": 128,
						"infiniburn": "#minecraft:infiniburn_nether",
						"ambient_light": 0.1,
						"monster_spawn_light_level": 7,
						"monster_spawn_block_light_limit": 15,
						"skybox": "none",
						"cardinal_light": "nether",
						"attributes": {
							"minecraft:visual/fog_start_distance": 10.0
						},
						"timelines": "#minecraft:in_nether"
					})json");
			}

			void WriteBiomeFixtures() const {
				WriteFile(
					"data/minecraft/worldgen/biome/plains.json",
					R"json({
						"has_precipitation": true,
						"temperature": 0.8,
						"downfall": 0.4,
						"effects": {
							"water_color": "#3f76e4"
						},
						"attributes": {
							"minecraft:visual/sky_color": "#78a7ff"
						},
						"features": [],
						"spawners": {}
					})json");

				WriteFile(
					"data/minecraft/worldgen/biome/frozen_test.json",
					R"json({
						"has_precipitation": true,
						"temperature": 0.0,
						"temperature_modifier": "frozen",
						"downfall": 0.5,
						"effects": {
							"water_color": 3750089,
							"foliage_color": "#60a17b",
							"dry_foliage_color": "#8c3a04",
							"grass_color": "#80b497",
							"grass_color_modifier": "swamp"
						},
						"attributes": {},
						"features": [],
						"spawners": {}
					})json");
			}

			void WriteNetworkFixtures() const {
				WriteFile(
					"data/minecraft/cat_variant/black.json",
					R"json({
						"asset_id": "minecraft:entity/cat/black",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/chicken_variant/cold.json",
					R"json({
						"model": "cold",
						"asset_id": "minecraft:entity/chicken/cold",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/cow_variant/warm.json",
					R"json({
						"model": "warm",
						"asset_id": "minecraft:entity/cow/warm",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/frog_variant/temperate.json",
					R"json({
						"asset_id": "minecraft:entity/frog/temperate",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/painting_variant/kebab.json",
					R"json({
						"width": 1,
						"height": 1,
						"asset_id": "minecraft:kebab",
						"title": {
							"translate": "painting.minecraft.kebab.title"
						},
						"author": {
							"text": "Kristoffer Zetterstrand"
						}
					})json");

				WriteFile(
					"data/minecraft/pig_variant/cold.json",
					R"json({
						"model": "cold",
						"asset_id": "minecraft:entity/pig/cold",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/wolf_sound_variant/classic.json",
					R"json({
						"ambient_sound": "minecraft:entity.wolf.ambient",
						"death_sound": "minecraft:entity.wolf.death",
						"growl_sound": "minecraft:entity.wolf.growl",
						"hurt_sound": "minecraft:entity.wolf.hurt",
						"pant_sound": "minecraft:entity.wolf.pant",
						"whine_sound": "minecraft:entity.wolf.whine"
					})json");

				WriteFile(
					"data/minecraft/wolf_variant/pale.json",
					R"json({
						"assets": {
							"wild": "minecraft:entity/wolf/wolf",
							"tame": "minecraft:entity/wolf/wolf_tame",
							"angry": "minecraft:entity/wolf/wolf_angry"
						},
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/zombie_nautilus_variant/warm.json",
					R"json({
						"model": "warm",
						"asset_id": "minecraft:entity/nautilus/zombie_warm",
						"spawn_conditions": []
					})json");

				WriteFile(
					"data/minecraft/chat_type/chat.json",
					R"json({
						"chat": {
							"translation_key": "chat.type.text",
							"parameters": ["sender", "content"]
						},
						"narration": {
							"translation_key": "chat.type.text.narrate",
							"parameters": ["sender", "content"]
						}
					})json");

				WriteFile(
					"data/minecraft/trim_pattern/sentry.json",
					R"json({
						"asset_id": "minecraft:sentry",
						"description": {
							"translate": "trim_pattern.minecraft.sentry"
						},
						"decal": false
					})json");

				WriteFile(
					"data/minecraft/trim_material/quartz.json",
					R"json({
						"asset_name": "quartz",
						"description": {
							"translate": "trim_material.minecraft.quartz"
						}
					})json");

				WriteFile(
					"data/minecraft/damage_type/generic.json",
					R"json({
						"message_id": "generic",
						"scaling": "when_caused_by_living_non_player",
						"exhaustion": 0.1
					})json");

				WriteFile(
					"data/minecraft/banner_pattern/base.json",
					R"json({
						"asset_id": "minecraft:base",
						"translation_key": "block.minecraft.banner.base"
					})json");

				WriteFile(
					"data/minecraft/enchantment/test.json",
					R"json({
						"description": {
							"translate": "enchantment.minecraft.test"
						},
						"supported_items": "#minecraft:enchantable/weapon",
						"weight": 10,
						"max_level": 1,
						"min_cost": {
							"base": 1,
							"per_level_above_first": 0
						},
						"max_cost": {
							"base": 11,
							"per_level_above_first": 0
						},
						"anvil_cost": 1,
						"slots": ["mainhand"]
					})json");

				WriteFile(
					"data/minecraft/jukebox_song/test.json",
					R"json({
						"sound_event": "minecraft:music_disc.13",
						"description": {
							"translate": "jukebox_song.minecraft.test"
						},
						"length_in_seconds": 178.0,
						"comparator_output": 1
					})json");

				WriteFile(
					"data/minecraft/instrument/test.json",
					R"json({
						"sound_event": "minecraft:item.goat_horn.sound.0",
						"use_duration": 7.0,
						"range": 256.0,
						"description": {
							"translate": "instrument.minecraft.test"
						}
					})json");

				WriteFile(
					"data/minecraft/test_environment/default.json",
					R"json({
						"type": "minecraft:time_of_day",
						"time": 6000
					})json");

				WriteFile(
					"data/minecraft/test_instance/basic.json",
					R"json({
						"type": "minecraft:block_based",
						"environment": "minecraft:default",
						"structure": "minecraft:basic",
						"max_ticks": 100
					})json");

				WriteFile(
					"data/minecraft/dialog/notice.json",
					R"json({
						"type": "minecraft:notice",
						"title": {
							"text": "Aurore"
						}
					})json");

				WriteFile(
					"data/minecraft/timeline/overworld.json",
					R"json({
						"period_ticks": 24000,
						"tracks": {
							"minecraft:visual/sky_color": {
								"value": "#78a7ff"
							},
							"minecraft:gameplay/can_start_raid": {
								"value": true
							}
						}
					})json");
			}

			[[nodiscard]] MinecraftDataCorpus MakeCorpus(
				std::string_view version = "1.21.11",
				std::int32_t protocol_version = 774) const {

				const auto result = MinecraftDataCorpusFactory::FromPreparation(
					RawDataPreparationResult{
						.Version = {
							.Id = std::string{ version },
							.ProtocolVersion = protocol_version,
						},
						.SourceSha1 = "0123456789abcdef0123456789abcdef01234567",
						.GenerationRoot = m_Root,
						.ReportsRoot = m_Root / "reports",
						.DataRoot = m_Root / "data",
					});

				EXPECT_TRUE(result.has_value());
				return result.value();
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(VanillaDataImporterTests, ImportsTypedRegistryDataForSupportedTarget) {
		const RegistryGeneration generation{ 17 };
		const auto result = VanillaDataImporter::Import(MakeCorpus(), generation);

		ASSERT_TRUE(result.has_value());
		ASSERT_NE(*result, nullptr);
		EXPECT_EQ((*result)->GetGeneration(), generation);

		EXPECT_TRUE((*result)->GetBlocks().Empty());
		EXPECT_TRUE((*result)->GetItems().Empty());
		EXPECT_EQ((*result)->GetNetworkRegistries().Size(), 21u);
		EXPECT_EQ((*result)->GetNetworkRegistries().GetTotalEntryCount(), 21u);
		EXPECT_EQ((*result)->GetNetworkRegistries().GetEntriesWithDataCount(), 21u);

		const auto& dimensions = (*result)->GetDimensionTypes();
		ASSERT_EQ(dimensions.Size(), 2u);

		const auto overworld_key = ParseRequired("minecraft:overworld");
		const auto nether_key = ParseRequired("minecraft:the_nether");

		const auto overworld_id = dimensions.FindRuntimeId(overworld_key);
		const auto nether_id = dimensions.FindRuntimeId(nether_key);
		ASSERT_TRUE(overworld_id.has_value());
		ASSERT_TRUE(nether_id.has_value());
		EXPECT_EQ(*overworld_id, 0u);
		EXPECT_EQ(*nether_id, 1u);

		const auto* overworld = dimensions.Find(overworld_key);
		ASSERT_NE(overworld, nullptr);

		const auto* minimum_y = overworld->Value.Data.Find(u"min_y");
		ASSERT_NE(minimum_y, nullptr);
		ASSERT_NE(minimum_y->AsInt(), nullptr);
		EXPECT_EQ(*minimum_y->AsInt(), -64);

		const auto* coordinate_scale = overworld->Value.Data.Find(u"coordinate_scale");
		ASSERT_NE(coordinate_scale, nullptr);
		ASSERT_NE(coordinate_scale->AsDouble(), nullptr);
		EXPECT_DOUBLE_EQ(*coordinate_scale->AsDouble(), 1.0);

		const auto* spawn_light = overworld->Value.Data.Find(u"monster_spawn_light_level");
		ASSERT_NE(spawn_light, nullptr);
		const auto* spawn_provider = spawn_light->AsCompound();
		ASSERT_NE(spawn_provider, nullptr);
		EXPECT_EQ(ReadRequiredText(*spawn_provider, u"type"), "minecraft:uniform");

		const auto* minimum_light = spawn_provider->Find(u"min_inclusive");
		const auto* maximum_light = spawn_provider->Find(u"max_inclusive");
		ASSERT_NE(minimum_light, nullptr);
		ASSERT_NE(maximum_light, nullptr);
		ASSERT_NE(minimum_light->AsInt(), nullptr);
		ASSERT_NE(maximum_light->AsInt(), nullptr);
		EXPECT_EQ(*minimum_light->AsInt(), 0);
		EXPECT_EQ(*maximum_light->AsInt(), 7);

		EXPECT_FALSE(overworld->Value.Data.Contains(u"attributes"));
		EXPECT_FALSE(overworld->Value.Data.Contains(u"timelines"));
		EXPECT_FALSE(overworld->Value.Data.Contains(u"has_fixed_time"));

		const auto* nether = dimensions.Find(nether_key);
		ASSERT_NE(nether, nullptr);

		const auto* has_fixed_time = nether->Value.Data.Find(u"has_fixed_time");
		ASSERT_NE(has_fixed_time, nullptr);
		ASSERT_NE(has_fixed_time->AsByte(), nullptr);
		EXPECT_EQ(*has_fixed_time->AsByte(), 1);

		const auto* nether_spawn_light = nether->Value.Data.Find(u"monster_spawn_light_level");
		ASSERT_NE(nether_spawn_light, nullptr);
		ASSERT_NE(nether_spawn_light->AsInt(), nullptr);
		EXPECT_EQ(*nether_spawn_light->AsInt(), 7);
		EXPECT_EQ(ReadRequiredText(nether->Value.Data, u"cardinal_light"), "nether");

		const auto& biomes = (*result)->GetBiomes();
		ASSERT_EQ(biomes.Size(), 2u);

		const auto frozen_key = ParseRequired("minecraft:frozen_test");
		const auto plains_key = ParseRequired("minecraft:plains");

		const auto frozen_id = biomes.FindRuntimeId(frozen_key);
		const auto plains_id = biomes.FindRuntimeId(plains_key);
		ASSERT_TRUE(frozen_id.has_value());
		ASSERT_TRUE(plains_id.has_value());
		EXPECT_EQ(*frozen_id, 0u);
		EXPECT_EQ(*plains_id, 1u);

		const auto* plains = biomes.Find(plains_key);
		ASSERT_NE(plains, nullptr);

		const auto* precipitation = plains->Value.Data.Find(u"has_precipitation");
		ASSERT_NE(precipitation, nullptr);
		ASSERT_NE(precipitation->AsByte(), nullptr);
		EXPECT_EQ(*precipitation->AsByte(), 1);

		const auto* temperature = plains->Value.Data.Find(u"temperature");
		ASSERT_NE(temperature, nullptr);
		ASSERT_NE(temperature->AsFloat(), nullptr);
		EXPECT_FLOAT_EQ(*temperature->AsFloat(), 0.8F);

		const auto* effects = plains->Value.Data.Find(u"effects");
		ASSERT_NE(effects, nullptr);
		const auto* effects_compound = effects->AsCompound();
		ASSERT_NE(effects_compound, nullptr);
		EXPECT_EQ(ReadRequiredText(*effects_compound, u"water_color"), "#3f76e4");

		EXPECT_FALSE(plains->Value.Data.Contains(u"attributes"));
		EXPECT_FALSE(plains->Value.Data.Contains(u"features"));
		EXPECT_FALSE(plains->Value.Data.Contains(u"spawners"));
		EXPECT_FALSE(plains->Value.Data.Contains(u"temperature_modifier"));

		const auto* frozen = biomes.Find(frozen_key);
		ASSERT_NE(frozen, nullptr);
		EXPECT_EQ(ReadRequiredText(frozen->Value.Data, u"temperature_modifier"), "frozen");

		const auto* frozen_effects_value = frozen->Value.Data.Find(u"effects");
		ASSERT_NE(frozen_effects_value, nullptr);
		const auto* frozen_effects = frozen_effects_value->AsCompound();
		ASSERT_NE(frozen_effects, nullptr);

		const auto* frozen_water = frozen_effects->Find(u"water_color");
		ASSERT_NE(frozen_water, nullptr);
		ASSERT_NE(frozen_water->AsInt(), nullptr);
		EXPECT_EQ(*frozen_water->AsInt(), 3750089);
		EXPECT_EQ(ReadRequiredText(*frozen_effects, u"grass_color_modifier"), "swamp");

		const auto& network = (*result)->GetNetworkRegistries();

		const auto cat_registry_key = ParseRequired("minecraft:cat_variant");
		const auto* cat_registry = network.Find(cat_registry_key);
		ASSERT_NE(cat_registry, nullptr);

		const auto cat_key = ParseRequired("minecraft:black");
		const auto* cat = cat_registry->Find(cat_key);
		ASSERT_NE(cat, nullptr);
		ASSERT_TRUE(cat->Value.has_value());
		EXPECT_EQ(ReadRequiredText(*cat->Value, u"asset_id"), "minecraft:entity/cat/black");
		EXPECT_FALSE(cat->Value->Contains(u"spawn_conditions"));

		const auto cow_registry_key = ParseRequired("minecraft:cow_variant");
		const auto* cow_registry = network.Find(cow_registry_key);
		ASSERT_NE(cow_registry, nullptr);

		const auto cow_key = ParseRequired("minecraft:warm");
		const auto* cow = cow_registry->Find(cow_key);
		ASSERT_NE(cow, nullptr);
		ASSERT_TRUE(cow->Value.has_value());
		EXPECT_EQ(ReadRequiredText(*cow->Value, u"model"), "warm");
		EXPECT_EQ(ReadRequiredText(*cow->Value, u"asset_id"), "minecraft:entity/cow/warm");
		EXPECT_FALSE(cow->Value->Contains(u"spawn_conditions"));

		const auto wolf_registry_key = ParseRequired("minecraft:wolf_variant");
		const auto* wolf_registry = network.Find(wolf_registry_key);
		ASSERT_NE(wolf_registry, nullptr);

		const auto wolf_key = ParseRequired("minecraft:pale");
		const auto* wolf = wolf_registry->Find(wolf_key);
		ASSERT_NE(wolf, nullptr);
		ASSERT_TRUE(wolf->Value.has_value());

		const auto* assets_value = wolf->Value->Find(u"assets");
		ASSERT_NE(assets_value, nullptr);
		const auto* assets = assets_value->AsCompound();
		ASSERT_NE(assets, nullptr);
		EXPECT_EQ(ReadRequiredText(*assets, u"wild"), "minecraft:entity/wolf/wolf");
		EXPECT_EQ(ReadRequiredText(*assets, u"tame"), "minecraft:entity/wolf/wolf_tame");
		EXPECT_EQ(ReadRequiredText(*assets, u"angry"), "minecraft:entity/wolf/wolf_angry");

		const auto sound_registry_key = ParseRequired("minecraft:wolf_sound_variant");
		const auto* sound_registry = network.Find(sound_registry_key);
		ASSERT_NE(sound_registry, nullptr);

		const auto classic_key = ParseRequired("minecraft:classic");
		const auto* classic = sound_registry->Find(classic_key);
		ASSERT_NE(classic, nullptr);
		ASSERT_TRUE(classic->Value.has_value());
		EXPECT_EQ(ReadRequiredText(*classic->Value, u"ambient_sound"), "minecraft:entity.wolf.ambient");
		EXPECT_EQ(ReadRequiredText(*classic->Value, u"whine_sound"), "minecraft:entity.wolf.whine");

		const auto painting_registry_key = ParseRequired("minecraft:painting_variant");
		const auto* painting_registry = network.Find(painting_registry_key);
		ASSERT_NE(painting_registry, nullptr);

		const auto painting_key = ParseRequired("minecraft:kebab");
		const auto* painting = painting_registry->Find(painting_key);
		ASSERT_NE(painting, nullptr);
		ASSERT_TRUE(painting->Value.has_value());

		const auto* width = painting->Value->Find(u"width");
		ASSERT_NE(width, nullptr);
		ASSERT_NE(width->AsInt(), nullptr);
		EXPECT_EQ(*width->AsInt(), 1);

		const auto* title_value = painting->Value->Find(u"title");
		ASSERT_NE(title_value, nullptr);
		const auto* title = title_value->AsCompound();
		ASSERT_NE(title, nullptr);
		EXPECT_EQ(ReadRequiredText(*title, u"translate"), "painting.minecraft.kebab.title");

		const auto damage_registry_key = ParseRequired("minecraft:damage_type");
		const auto* damage_registry = network.Find(damage_registry_key);
		ASSERT_NE(damage_registry, nullptr);

		const auto damage_key = ParseRequired("minecraft:generic");
		const auto* damage = damage_registry->Find(damage_key);
		ASSERT_NE(damage, nullptr);
		ASSERT_TRUE(damage->Value.has_value());
		EXPECT_EQ(ReadRequiredText(*damage->Value, u"message_id"), "generic");
		EXPECT_EQ(ReadRequiredText(*damage->Value, u"scaling"), "when_caused_by_living_non_player");

		const auto timeline_registry_key = ParseRequired("minecraft:timeline");
		const auto* timeline_registry = network.Find(timeline_registry_key);
		ASSERT_NE(timeline_registry, nullptr);

		const auto timeline_key = ParseRequired("minecraft:overworld");
		const auto* timeline = timeline_registry->Find(timeline_key);
		ASSERT_NE(timeline, nullptr);
		ASSERT_TRUE(timeline->Value.has_value());

		const auto* tracks_value = timeline->Value->Find(u"tracks");
		ASSERT_NE(tracks_value, nullptr);
		const auto* tracks = tracks_value->AsCompound();
		ASSERT_NE(tracks, nullptr);

		EXPECT_TRUE(tracks->Contains(u"minecraft:visual/sky_color"));
		EXPECT_FALSE(tracks->Contains(u"minecraft:gameplay/can_start_raid"));
	}

	TEST_F(VanillaDataImporterTests, RejectsUnsupportedMinecraftVersion) {
		const auto result = VanillaDataImporter::Import(MakeCorpus("1.21.10", 774), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::UnsupportedMinecraftVersion);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::TargetValidation);
		EXPECT_EQ(result.error().MinecraftVersion, "1.21.10");
		EXPECT_EQ(result.error().ProtocolVersion, 774);
		EXPECT_EQ(result.error().Generation, 1u);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}

	TEST_F(VanillaDataImporterTests, RejectsUnsupportedProtocolVersion) {
		const auto result = VanillaDataImporter::Import(MakeCorpus("1.21.11", 773), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::UnsupportedProtocolVersion);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::TargetValidation);
		EXPECT_EQ(result.error().MinecraftVersion, "1.21.11");
		EXPECT_EQ(result.error().ProtocolVersion, 773);
		EXPECT_EQ(result.error().Generation, 1u);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}

	TEST_F(VanillaDataImporterTests, RejectsInvalidGeneration) {
		const auto result = VanillaDataImporter::Import(
			MakeCorpus(),
			Aurore::Util::NoRegistryGeneration);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::InvalidGeneration);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::TargetValidation);
		EXPECT_EQ(result.error().Generation, Aurore::Util::NoRegistryGeneration);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}

	TEST_F(VanillaDataImporterTests, RejectsMissingDimensionTypeSource) {
		std::filesystem::remove_all(m_Root / "data" / "minecraft" / "dimension_type");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::MissingRegistrySource);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::DimensionTypes);
		EXPECT_EQ(result.error().Path, m_Root / "data" / "minecraft" / "dimension_type");
	}

	TEST_F(VanillaDataImporterTests, RejectsMalformedBiomeJson) {
		WriteFile("data/minecraft/worldgen/biome/plains.json", "{");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::JsonParseFailed);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::Biomes);
		ASSERT_TRUE(result.error().Key.has_value());
		EXPECT_EQ(*result.error().Key, ParseRequired("minecraft:plains"));
		EXPECT_FALSE(result.error().Detail.empty());
	}

	TEST_F(VanillaDataImporterTests, RejectsInvalidDimensionHeight) {
		WriteFile(
			"data/minecraft/dimension_type/overworld.json",
			R"json({
				"has_skylight": true,
				"has_ceiling": false,
				"coordinate_scale": 1.0,
				"min_y": -64,
				"height": 385,
				"logical_height": 384,
				"infiniburn": "#minecraft:infiniburn_overworld",
				"ambient_light": 0.0,
				"monster_spawn_light_level": 7,
				"monster_spawn_block_light_limit": 0
			})json");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::InvalidFieldValue);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::DimensionTypes);
		EXPECT_EQ(result.error().Field, "height");
		ASSERT_TRUE(result.error().Key.has_value());
		EXPECT_EQ(*result.error().Key, ParseRequired("minecraft:overworld"));
	}

	TEST_F(VanillaDataImporterTests, RejectsMissingNetworkRegistrySource) {
		std::filesystem::remove_all(m_Root / "data" / "minecraft" / "cat_variant");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::MissingRegistrySource);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::NetworkRegistries);
		EXPECT_EQ(result.error().Path, m_Root / "data" / "minecraft" / "cat_variant");
	}

	TEST_F(VanillaDataImporterTests, RejectsUnexpectedNetworkRegistryField) {
		WriteFile(
			"data/minecraft/cat_variant/black.json",
			R"json({
				"asset_id": "minecraft:entity/cat/black",
				"spawn_conditions": [],
				"future_field": true
			})json");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::UnexpectedField);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::NetworkRegistries);
		EXPECT_EQ(result.error().Field, "future_field");
		ASSERT_TRUE(result.error().Key.has_value());
		EXPECT_EQ(*result.error().Key, ParseRequired("minecraft:black"));
	}


	TEST_F(VanillaDataImporterTests, RejectsUnknownTimelineEnvironmentAttribute) {
		WriteFile(
			"data/minecraft/timeline/overworld.json",
			R"json({
				"tracks": {
					"minecraft:visual/future_attribute": {
						"value": 1
					}
				}
			})json");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::InvalidFieldValue);
		EXPECT_EQ(result.error().Stage, VanillaDataImportStage::NetworkRegistries);
		EXPECT_EQ(result.error().Field, "minecraft:visual/future_attribute");
		ASSERT_TRUE(result.error().Key.has_value());
		EXPECT_EQ(*result.error().Key, ParseRequired("minecraft:overworld"));
	}


	TEST_F(VanillaDataImporterTests, AllowsMissingOptionalEmptyNetworkRegistrySource) {
		std::filesystem::remove_all(m_Root / "data" / "minecraft" / "test_instance");

		const auto result = VanillaDataImporter::Import(MakeCorpus(), 1);

		ASSERT_TRUE(result.has_value());

		const auto registry_key = ParseRequired("minecraft:test_instance");
		const auto* registry = (*result)->GetNetworkRegistries().Find(registry_key);
		ASSERT_NE(registry, nullptr);
		EXPECT_TRUE(registry->Empty());
	}

}
