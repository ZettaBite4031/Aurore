#include <Aurore/Protocol/PlayChunk.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Protocol::EncodeLevelChunkWithLight;
		using Aurore::Protocol::PlayChunkBlock;
		using Aurore::Protocol::PlayChunkColumn;
		using Aurore::Util::BiomeDefinition;
		using Aurore::Util::BlockDefinition;
		using Aurore::Util::ByteReader;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::RegistrySnapshotBuilder;
		using Aurore::Util::ResourceLocation;

		[[nodiscard]] ResourceLocation Location(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			if (!result) throw std::runtime_error("invalid test resource location");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> MakeSnapshot() {
			RegistrySnapshotBuilder builder;
			if (!builder.Blocks().Declare(Location("minecraft:air"), BlockDefinition{ .DefaultStateId = 0 })) throw std::runtime_error("air");
			if (!builder.Blocks().Declare(Location("minecraft:stone"), BlockDefinition{ .DefaultStateId = 1 })) throw std::runtime_error("stone");
			if (!builder.Blocks().Declare(Location("minecraft:dirt"), BlockDefinition{ .DefaultStateId = 2 })) throw std::runtime_error("dirt");
			if (!builder.Blocks().Declare(Location("minecraft:grass_block"), BlockDefinition{ .DefaultStateId = 3 })) throw std::runtime_error("grass");
			if (!builder.Biomes().Declare(Location("minecraft:plains"), BiomeDefinition{})) throw std::runtime_error("plains");

			auto result = std::move(builder).Build(1);
			if (!result) throw std::runtime_error("snapshot");
			return std::move(*result);
		}

		[[nodiscard]] PlayChunkColumn MakeColumn() {
			PlayChunkColumn result{
				.X = 0,
				.Z = 0,
				.MinimumY = -64,
				.Height = 384,
				.AirBlock = Location("minecraft:air"),
				.Biome = Location("minecraft:plains"),
			};

			for (std::uint8_t z{ 0 }; z < 5; ++z) {
				for (std::uint8_t x{ 0 }; x < 5; ++x) {
					result.Blocks.push_back(PlayChunkBlock{ .X = x, .Y = 61, .Z = z, .Block = Location("minecraft:stone") });
					result.Blocks.push_back(PlayChunkBlock{ .X = x, .Y = 62, .Z = z, .Block = Location("minecraft:dirt") });
					result.Blocks.push_back(PlayChunkBlock{ .X = x, .Y = 63, .Z = z, .Block = Location("minecraft:grass_block") });
				}
			}
			return result;
		}

		[[nodiscard]] std::uint64_t ReadBitSet(ByteReader& reader) {
			const auto words = reader.ReadVarInt();
			if (!words || *words < 0 || *words > 1) throw std::runtime_error("unexpected bitset size");
			if (*words == 0) return 0;
			const auto word = reader.ReadInt64();
			if (!word) throw std::runtime_error("truncated bitset");
			return static_cast<std::uint64_t>(*word);
		}

		struct Container final {
			std::uint8_t Bits{ 0 };
			std::vector<std::uint32_t> Palette;
			std::vector<std::uint32_t> Values;
		};

		[[nodiscard]] Container ReadContainer(ByteReader& reader, std::size_t entries) {
			const auto bits_value = reader.ReadUnsignedByte();
			if (!bits_value) throw std::runtime_error("missing bits");
			const auto bits = *bits_value;

			Container result{ .Bits = bits };
			if (bits == 0) {
				const auto value = reader.ReadVarInt();
				if (!value) throw std::runtime_error("missing single palette value");
				result.Palette.push_back(static_cast<std::uint32_t>(*value));
				result.Values.assign(entries, result.Palette.front());
				return result;
			}

			const auto palette_size = reader.ReadVarInt();
			if (!palette_size || *palette_size <= 0) throw std::runtime_error("bad palette size");
			for (std::int32_t index{ 0 }; index < *palette_size; ++index) {
				const auto value = reader.ReadVarInt();
				if (!value) throw std::runtime_error("truncated palette");
				result.Palette.push_back(static_cast<std::uint32_t>(*value));
			}

			const auto per_long = static_cast<std::size_t>(64u / bits);
			const auto long_count = (entries + per_long - 1) / per_long;
			result.Values.reserve(entries);
			const auto mask = (std::uint64_t{ 1 } << bits) - 1u;
			for (std::size_t long_index{ 0 }; long_index < long_count; ++long_index) {
				const auto raw = reader.ReadInt64();
				if (!raw) throw std::runtime_error("truncated packed data");
				const auto word = static_cast<std::uint64_t>(*raw);
				for (std::size_t slot{ 0 }; slot < per_long && result.Values.size() < entries; ++slot) {
					const auto palette_index = static_cast<std::size_t>((word >> (slot * bits)) & mask);
					if (palette_index >= result.Palette.size()) throw std::runtime_error("palette index out of range");
					result.Values.push_back(result.Palette[palette_index]);
				}
			}
			return result;
		}
	}

	TEST(ChunkDeliveryTests, EncodesProtocol774FirstLightColumn) {
		const auto snapshot = MakeSnapshot();
		const auto encoded = EncodeLevelChunkWithLight(MakeColumn(), *snapshot);
		ASSERT_TRUE(encoded.has_value());
		EXPECT_EQ(encoded->PacketId, Aurore::Protocol::Packets::Play::Clientbound::LevelChunkWithLightPacketId);

		ByteReader packet(encoded->Payload);
		ASSERT_EQ(*packet.ReadInt32(), 0);
		ASSERT_EQ(*packet.ReadInt32(), 0);
		ASSERT_EQ(*packet.ReadVarInt(), 0); // no heightmaps

		const auto data_size = packet.ReadVarInt();
		ASSERT_TRUE(data_size.has_value());
		ASSERT_GT(*data_size, 0);
		const auto data = packet.ReadBytes(static_cast<std::size_t>(*data_size));
		ASSERT_TRUE(data.has_value());

		ByteReader sections(*data);
		for (std::size_t section{ 0 }; section < 24; ++section) {
			const auto count = sections.ReadInt16();
			ASSERT_TRUE(count.has_value());
			const auto blocks = ReadContainer(sections, 4096);
			const auto biomes = ReadContainer(sections, 64);

			ASSERT_EQ(biomes.Palette.size(), 1u);
			EXPECT_EQ(biomes.Palette.front(), 0u);

			if (section != 7) {
				EXPECT_EQ(*count, 0);
				ASSERT_EQ(blocks.Palette.size(), 1u);
				EXPECT_EQ(blocks.Palette.front(), 0u);
				continue;
			}

			EXPECT_EQ(*count, 75);
			EXPECT_EQ(blocks.Bits, 4);
			EXPECT_EQ(blocks.Palette, (std::vector<std::uint32_t>{ 0, 1, 2, 3 }));
			const auto at = [&blocks](std::uint8_t x, std::uint8_t y, std::uint8_t z) {
				return blocks.Values[(static_cast<std::size_t>(y) * 256u) + (static_cast<std::size_t>(z) * 16u) + x];
			};
			EXPECT_EQ(at(2, 13, 2), 1u); // Y=61 stone
			EXPECT_EQ(at(2, 14, 2), 2u); // Y=62 dirt
			EXPECT_EQ(at(2, 15, 2), 3u); // Y=63 grass
			EXPECT_EQ(at(6, 15, 6), 0u); // outside platform
		}
		EXPECT_TRUE(sections.Empty());

		ASSERT_EQ(*packet.ReadVarInt(), 0); // block entities

		constexpr std::uint64_t AllLightSections = (std::uint64_t{ 1 } << 26) - 1u;
		EXPECT_EQ(ReadBitSet(packet), AllLightSections);
		EXPECT_EQ(ReadBitSet(packet), 0u);
		EXPECT_EQ(ReadBitSet(packet), 0u);
		EXPECT_EQ(ReadBitSet(packet), AllLightSections);

		ASSERT_EQ(*packet.ReadVarInt(), 26);
		for (int section{ 0 }; section < 26; ++section) {
			ASSERT_EQ(*packet.ReadVarInt(), 2048);
			const auto light = packet.ReadBytes(2048);
			ASSERT_TRUE(light.has_value());
			for (const auto byte : *light)
				ASSERT_EQ(byte, std::byte{ 0xFF });
		}
		ASSERT_EQ(*packet.ReadVarInt(), 0);
		EXPECT_TRUE(packet.Empty());
	}

	TEST(ChunkDeliveryTests, RejectsUnknownWorldBlock) {
		auto column = MakeColumn();
		column.Blocks.push_back(PlayChunkBlock{
			.X = 8,
			.Y = 63,
			.Z = 8,
			.Block = Location("minecraft:definitely_missing"),
		});

		const auto result = EncodeLevelChunkWithLight(column, *MakeSnapshot());
		EXPECT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), Aurore::Protocol::ProtocolError::PlayBootstrapFailed);
	}
}
