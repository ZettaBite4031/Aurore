#include <Aurore/Util/ResourceLocation.hpp>

#include <Aurore/World/Chunk.hpp>
#include <Aurore/World/Dimension.hpp>
#include <Aurore/World/WorldManager.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>
#include <utility>

namespace Aurore::Tests {
	namespace {
		[[nodiscard]] Aurore::Util::ResourceLocation Location(std::string_view value) {
			auto result = Aurore::Util::ResourceLocation::Parse(value);
			if (!result)
				throw std::runtime_error("Invalid world-representation test ResourceLocation.");
			return std::move(*result);
		}
	}

	TEST(WorldRepresentationTests, InitializesFirstLightOverworldAsVoidWorld) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());

		const auto* overworld = worlds.GetOverworld();
		ASSERT_NE(overworld, nullptr);
		EXPECT_TRUE(overworld->IsVoidWorld());
		EXPECT_EQ(overworld->GetMinimumY(), -64);
		EXPECT_EQ(overworld->GetHeight(), 384);
		EXPECT_EQ(overworld->GetSeaLevel(), 63);
		EXPECT_EQ(overworld->GetLoadedChunkCount(), 1u);
	}

	TEST(WorldRepresentationTests, BuildsFiveByFiveThreeLayerFirstLightPlatform) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		const auto* overworld = worlds.GetOverworld();
		ASSERT_NE(overworld, nullptr);

		const auto grass = Location("minecraft:grass_block");
		const auto dirt = Location("minecraft:dirt");
		const auto stone = Location("minecraft:stone");
		const auto air = Location("minecraft:air");

		for (std::int32_t z{ 0 }; z < 5; ++z) {
			for (std::int32_t x{ 0 }; x < 5; ++x) {
				EXPECT_EQ(overworld->GetBlock({ x, 63, z }), grass);
				EXPECT_EQ(overworld->GetBlock({ x, 62, z }), dirt);
				EXPECT_EQ(overworld->GetBlock({ x, 61, z }), stone);
				EXPECT_EQ(overworld->GetBlock({ x, 64, z }), air);
			}
		}

		EXPECT_EQ(overworld->GetBlock({ 5, 63, 0 }), air);
		EXPECT_EQ(overworld->GetBlock({ 0, 63, 5 }), air);
		EXPECT_EQ(overworld->GetBlock({ -1, 63, 0 }), air);
	}

	TEST(WorldRepresentationTests, SpawnIsCenteredAbovePlatform) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		const auto* overworld = worlds.GetOverworld();
		ASSERT_NE(overworld, nullptr);

		const auto& spawn = overworld->GetSpawnPosition();
		EXPECT_DOUBLE_EQ(spawn.X, 2.5);
		EXPECT_DOUBLE_EQ(spawn.Y, 64.0);
		EXPECT_DOUBLE_EQ(spawn.Z, 2.5);
		EXPECT_FLOAT_EQ(spawn.Yaw, 0.0F);
		EXPECT_FLOAT_EQ(spawn.Pitch, 0.0F);
	}

	TEST(WorldRepresentationTests, ChunkCoordinateConversionHandlesNegativeBlocks) {
		EXPECT_EQ(
			Aurore::World::GetChunkPosition({ -1, 64, -1 }),
			(Aurore::World::ChunkPosition{ -1, -1 }));

		EXPECT_EQ(
			Aurore::World::GetLocalBlockPosition({ -1, 64, -1 }),
			(Aurore::World::LocalBlockPosition{ 15, 64, 15 }));

		EXPECT_EQ(
			Aurore::World::GetChunkPosition({ -16, 64, -16 }),
			(Aurore::World::ChunkPosition{ -1, -1 }));

		EXPECT_EQ(
			Aurore::World::GetLocalBlockPosition({ -16, 64, -16 }),
			(Aurore::World::LocalBlockPosition{ 0, 64, 0 }));
	}

	TEST(WorldRepresentationTests, SettingAirRemovesSparseBlockStorage) {
		auto air = Location("minecraft:air");
		Aurore::World::Chunk chunk(
			{ 0, 0 },
			-64,
			384,
			air);

		ASSERT_TRUE(chunk.SetBlock({ 1, 70, 1 }, Location("minecraft:stone")));
		EXPECT_EQ(chunk.StoredBlockCount(), 1u);
		EXPECT_EQ(chunk.GetBlock({ 1, 70, 1 }), Location("minecraft:stone"));

		ASSERT_TRUE(chunk.SetBlock({ 1, 70, 1 }, air));
		EXPECT_TRUE(chunk.Empty());
		EXPECT_EQ(chunk.GetBlock({ 1, 70, 1 }), Location("minecraft:air"));
	}

	TEST(WorldRepresentationTests, DimensionRoutesBlocksAcrossChunkBoundaries) {
		Aurore::World::Dimension dimension(
			Aurore::World::DimensionKind::Custom,
			"aurore_test:void");
		ASSERT_TRUE(dimension.Initialize());

		ASSERT_TRUE(dimension.SetBlock(
			{ -1, 80, -1 },
			Location("minecraft:stone")));

		EXPECT_EQ(dimension.GetLoadedChunkCount(), 1u);
		ASSERT_NE(dimension.FindChunk({ -1, -1 }), nullptr);
		EXPECT_EQ(
			dimension.GetBlock({ -1, 80, -1 }),
			Location("minecraft:stone"));
	}
}
