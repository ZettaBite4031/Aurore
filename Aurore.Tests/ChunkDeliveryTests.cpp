#include <Aurore/Protocol/PlayChunk.hpp>

#include <Aurore/Data/MinecraftDataCorpus.hpp>
#include <Aurore/Data/VanillaDataImporter.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::MinecraftDataCorpusFactory;
		using Aurore::Data::RawDataPreparationResult;
		using Aurore::Data::VanillaDataImporter;
		using Aurore::Protocol::EncodeLevelChunkWithLight;
		using Aurore::Protocol::PlayChunkBlock;
		using Aurore::Protocol::PlayChunkColumn;
		using Aurore::Protocol::PlayChunkEncodingDiagnostics;
		using Aurore::Protocol::PlayHeightmapType;
		using Aurore::Protocol::ResolvedBlockStateDiagnostic;
		using Aurore::Util::BiomeDefinition;
		using Aurore::Util::BlockDefinition;
		using Aurore::Util::ByteReader;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::RegistrySnapshotBuilder;
		using Aurore::Util::ResourceLocation;

		[[nodiscard]] ResourceLocation Location(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			if (!result)
				throw std::runtime_error(
					"invalid test resource location");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> MakeSnapshot() {
			RegistrySnapshotBuilder builder;

			if (!builder.Blocks().Declare(
				Location("minecraft:air"),
				BlockDefinition{
					.DefaultStateId = 0 }))
				throw std::runtime_error("air");

			if (!builder.Blocks().Declare(
				Location("minecraft:stone"),
				BlockDefinition{
					.DefaultStateId = 1 }))
				throw std::runtime_error("stone");

			if (!builder.Blocks().Declare(
				Location("minecraft:dirt"),
				BlockDefinition{
					.DefaultStateId = 2 }))
				throw std::runtime_error("dirt");

			if (!builder.Blocks().Declare(
				Location("minecraft:grass_block"),
				BlockDefinition{
					.DefaultStateId = 3 }))
				throw std::runtime_error("grass");

			if (!builder.Biomes().Declare(
				Location("minecraft:plains"),
				BiomeDefinition{}))
				throw std::runtime_error("plains");

			auto result =
				std::move(builder).Build(1);

			if (!result)
				throw std::runtime_error(
					"snapshot");

			return std::move(*result);
		}

		[[nodiscard]] PlayChunkColumn MakeColumn() {
			PlayChunkColumn result{
				.X = 0,
				.Z = 0,
				.MinimumY = -64,
				.Height = 384,
				.AirBlock =
					Location("minecraft:air"),
				.Biome =
					Location("minecraft:plains"),
			};

			for (std::uint8_t z{ 0 };
				z < 5;
				++z) {

				for (std::uint8_t x{ 0 };
					x < 5;
					++x) {

					result.Blocks.push_back(
						PlayChunkBlock{
							.X = x,
							.Y = 61,
							.Z = z,
							.Block =
								Location(
									"minecraft:stone"),
						});

					result.Blocks.push_back(
						PlayChunkBlock{
							.X = x,
							.Y = 62,
							.Z = z,
							.Block =
								Location(
									"minecraft:dirt"),
						});

					result.Blocks.push_back(
						PlayChunkBlock{
							.X = x,
							.Y = 63,
							.Z = z,
							.Block =
								Location(
									"minecraft:grass_block"),
						});
				}
			}

			return result;
		}

		[[nodiscard]] const ResolvedBlockStateDiagnostic* FindResolved(
			const PlayChunkEncodingDiagnostics& diagnostics,
			std::string_view key) {

			const auto location =
				Location(key);

			const auto iterator =
				std::find_if(
					diagnostics.ResolvedBlockStates.begin(),
					diagnostics.ResolvedBlockStates.end(),
					[&location](
						const ResolvedBlockStateDiagnostic& entry) {
						return entry.Block == location;
					});

			return iterator ==
				diagnostics.ResolvedBlockStates.end()
				? nullptr
				: &*iterator;
		}

		[[nodiscard]] std::uint64_t ReadBitSet(
			ByteReader& reader) {

			const auto words =
				reader.ReadVarInt();

			if (!words
				|| *words < 0
				|| *words > 1)
				throw std::runtime_error(
					"unexpected bitset size");

			if (*words == 0)
				return 0;

			const auto word =
				reader.ReadInt64();

			if (!word)
				throw std::runtime_error(
					"truncated bitset");

			return static_cast<std::uint64_t>(
				*word);
		}

		struct Container final {
			std::uint8_t Bits{ 0 };
			std::vector<std::uint32_t> Palette;
			std::vector<std::uint32_t> Values;
		};

		struct Heightmap final {
			std::int32_t Type{ 0 };
			std::vector<std::int64_t> Longs;
			std::vector<std::uint32_t> Values;
		};

		[[nodiscard]] std::vector<std::uint32_t> UnpackValues(
			std::span<const std::int64_t> longs,
			std::size_t entries,
			std::uint8_t bits) {

			const auto per_long =
				static_cast<std::size_t>(
					64u / bits);

			const auto mask =
				(std::uint64_t{ 1 } << bits)
				- 1u;

			std::vector<std::uint32_t> values;
			values.reserve(entries);

			for (const auto raw : longs) {
				const auto word =
					static_cast<std::uint64_t>(
						raw);

				for (std::size_t slot{ 0 };
					slot < per_long
					&& values.size() < entries;
					++slot) {

					values.push_back(
						static_cast<std::uint32_t>(
							(word
								>> (slot * bits))
							& mask));
				}
			}

			if (values.size() != entries)
				throw std::runtime_error(
					"heightmap entry count mismatch");

			return values;
		}

		[[nodiscard]] std::vector<Heightmap> ReadHeightmaps(
			ByteReader& reader,
			std::uint8_t bits) {

			const auto count =
				reader.ReadVarInt();

			if (!count || *count < 0)
				throw std::runtime_error(
					"invalid heightmap count");

			std::vector<Heightmap> result;
			result.reserve(
				static_cast<std::size_t>(*count));

			for (std::int32_t index{ 0 };
				index < *count;
				++index) {

				const auto type =
					reader.ReadVarInt();
				const auto long_count =
					reader.ReadVarInt();

				if (!type
					|| !long_count
					|| *long_count < 0)
					throw std::runtime_error(
						"invalid heightmap");

				Heightmap heightmap{
					.Type = *type,
				};

				heightmap.Longs.reserve(
					static_cast<std::size_t>(
						*long_count));

				for (std::int32_t word{ 0 };
					word < *long_count;
					++word) {

					const auto value =
						reader.ReadInt64();

					if (!value)
						throw std::runtime_error(
							"truncated heightmap");

					heightmap.Longs.push_back(
						*value);
				}

				heightmap.Values =
					UnpackValues(
						heightmap.Longs,
						256,
						bits);

				result.push_back(
					std::move(heightmap));
			}

			return result;
		}

		[[nodiscard]] Container ReadContainer(
			ByteReader& reader,
			std::size_t entries) {

			const auto bits_value =
				reader.ReadUnsignedByte();

			if (!bits_value)
				throw std::runtime_error(
					"missing bits");

			const auto bits =
				*bits_value;

			Container result{
				.Bits = bits
			};

			if (bits == 0) {
				const auto value =
					reader.ReadVarInt();

				if (!value)
					throw std::runtime_error(
						"missing single palette value");

				result.Palette.push_back(
					static_cast<std::uint32_t>(
						*value));

				result.Values.assign(
					entries,
					result.Palette.front());

				return result;
			}

			const auto palette_size =
				reader.ReadVarInt();

			if (!palette_size
				|| *palette_size <= 0)
				throw std::runtime_error(
					"bad palette size");

			for (std::int32_t index{ 0 };
				index < *palette_size;
				++index) {

				const auto value =
					reader.ReadVarInt();

				if (!value)
					throw std::runtime_error(
						"truncated palette");

				result.Palette.push_back(
					static_cast<std::uint32_t>(
						*value));
			}

			const auto per_long =
				static_cast<std::size_t>(
					64u / bits);

			const auto long_count =
				(entries + per_long - 1)
				/ per_long;

			result.Values.reserve(entries);

			const auto mask =
				(std::uint64_t{ 1 } << bits)
				- 1u;

			for (std::size_t long_index{ 0 };
				long_index < long_count;
				++long_index) {

				const auto raw =
					reader.ReadInt64();

				if (!raw)
					throw std::runtime_error(
						"truncated packed data");

				const auto word =
					static_cast<std::uint64_t>(
						*raw);

				for (std::size_t slot{ 0 };
					slot < per_long
					&& result.Values.size()
						< entries;
					++slot) {

					const auto palette_index =
						static_cast<std::size_t>(
							(word
								>> (slot * bits))
							& mask);

					if (palette_index
						>= result.Palette.size())
						throw std::runtime_error(
							"palette index out of range");

					result.Values.push_back(
						result.Palette[
							palette_index]);
				}
			}

			return result;
		}

		[[nodiscard]] std::size_t CountValue(
			const std::vector<std::uint32_t>& values,
			std::uint32_t value) {

			return static_cast<std::size_t>(
				std::count(
					values.begin(),
					values.end(),
					value));
		}

		[[nodiscard]] std::size_t HeightmapIndex(
			std::uint8_t x,
			std::uint8_t z) {

			return static_cast<std::size_t>(z)
				* 16u
				+ static_cast<std::size_t>(x);
		}

		void ExpectFirstLightHeightmap(
			const std::vector<std::uint32_t>& values) {

			ASSERT_EQ(values.size(), 256u);

			for (std::uint8_t z{ 0 };
				z < 16;
				++z) {

				for (std::uint8_t x{ 0 };
					x < 16;
					++x) {

					const auto expected =
						(x < 5 && z < 5)
						? 128u
						: 0u;

					EXPECT_EQ(
						values[
							HeightmapIndex(
								x,
								z)],
						expected);
				}
			}

			EXPECT_EQ(
				CountValue(values, 128u),
				25u);

			EXPECT_EQ(
				CountValue(values, 0u),
				231u);
		}
	}

	TEST(ChunkDeliveryTests, ReportsResolvedFirstLightStateIds) {
		const auto snapshot =
			MakeSnapshot();

		PlayChunkEncodingDiagnostics diagnostics;

		const auto encoded =
			EncodeLevelChunkWithLight(
				MakeColumn(),
				*snapshot,
				&diagnostics);

		ASSERT_TRUE(encoded.has_value());

		EXPECT_EQ(
			diagnostics.AirStateId,
			0u);
		EXPECT_EQ(
			diagnostics.TotalNonAirBlocks,
			75u);

		ASSERT_EQ(
			diagnostics.Sections.size(),
			24u);

		const auto* air =
			FindResolved(
				diagnostics,
				"minecraft:air");
		const auto* stone =
			FindResolved(
				diagnostics,
				"minecraft:stone");
		const auto* dirt =
			FindResolved(
				diagnostics,
				"minecraft:dirt");
		const auto* grass =
			FindResolved(
				diagnostics,
				"minecraft:grass_block");

		ASSERT_NE(air, nullptr);
		ASSERT_NE(stone, nullptr);
		ASSERT_NE(dirt, nullptr);
		ASSERT_NE(grass, nullptr);

		EXPECT_EQ(air->StateId, 0u);
		EXPECT_EQ(stone->StateId, 1u);
		EXPECT_EQ(dirt->StateId, 2u);
		EXPECT_EQ(grass->StateId, 3u);

		for (std::size_t index{ 0 };
			index < diagnostics.Sections.size();
			++index) {

			const auto& section =
				diagnostics.Sections[index];

			EXPECT_EQ(
				section.Index,
				index);

			EXPECT_EQ(
				section.MinimumY,
				-64
				+ static_cast<std::int32_t>(
					index * 16));

			EXPECT_EQ(
				section.MaximumY,
				section.MinimumY + 15);

			if (index == 7) {
				EXPECT_EQ(
					section.MinimumY,
					48);
				EXPECT_EQ(
					section.MaximumY,
					63);
				EXPECT_EQ(
					section.NonAirBlocks,
					75);
				EXPECT_EQ(
					section.BitsPerBlock,
					4u);
				EXPECT_EQ(
					section.Palette,
					(std::vector<
						std::uint32_t>{
							0, 1, 2, 3 }));
			}
			else {
				EXPECT_EQ(
					section.NonAirBlocks,
					0);
				EXPECT_EQ(
					section.BitsPerBlock,
					0u);
				EXPECT_EQ(
					section.Palette,
					(std::vector<
						std::uint32_t>{
							0 }));
			}
		}
	}

	TEST(ChunkDeliveryTests, EncodesFirstLightHeightmaps) {
		const auto snapshot =
			MakeSnapshot();

		PlayChunkEncodingDiagnostics diagnostics;

		const auto encoded =
			EncodeLevelChunkWithLight(
				MakeColumn(),
				*snapshot,
				&diagnostics);

		ASSERT_TRUE(encoded.has_value());

		ASSERT_EQ(
			diagnostics.Heightmaps.size(),
			2u);

		EXPECT_EQ(
			diagnostics.Heightmaps[0].Type,
			PlayHeightmapType::WorldSurface);

		EXPECT_EQ(
			diagnostics.Heightmaps[1].Type,
			PlayHeightmapType::MotionBlocking);

		for (const auto& heightmap :
			diagnostics.Heightmaps) {

			EXPECT_EQ(
				heightmap.BitsPerEntry,
				9u);

			EXPECT_EQ(
				heightmap.PackedLongs.size(),
				37u);

			ExpectFirstLightHeightmap(
				heightmap.Values);
		}
	}

	TEST(ChunkDeliveryTests, EncodesImplicitAirNeighborWithOneSectionCountShort) {
		const auto snapshot = MakeSnapshot();
		auto neighbor = MakeColumn();
		neighbor.X = -1;
		neighbor.Z = 1;
		neighbor.Blocks.clear();

		PlayChunkEncodingDiagnostics diagnostics;
		const auto encoded = EncodeLevelChunkWithLight(neighbor, *snapshot, &diagnostics);
		ASSERT_TRUE(encoded.has_value());
		ASSERT_EQ(diagnostics.Sections.size(), 24u);
		EXPECT_EQ(diagnostics.TotalNonAirBlocks, 0u);
		for (const auto& section : diagnostics.Sections) {
			EXPECT_EQ(section.NonAirBlocks, 0);
			EXPECT_EQ(section.BitsPerBlock, 0u);
			EXPECT_EQ(section.Palette, (std::vector<std::uint32_t>{0}));
		}
		ASSERT_EQ(diagnostics.Heightmaps.size(), 2u);
		for (const auto& map : diagnostics.Heightmaps) {
			EXPECT_EQ(CountValue(map.Values, 0u), 256u);
		}
		EXPECT_EQ(diagnostics.Light.SkyLightArrayCount, 26u);
		EXPECT_EQ(diagnostics.Light.BlockLightArrayCount, 0u);

		ByteReader packet(encoded->Payload);
		ASSERT_EQ(*packet.ReadInt32(), -1);
		ASSERT_EQ(*packet.ReadInt32(), 1);
		const auto maps = ReadHeightmaps(packet, 9);
		ASSERT_EQ(maps.size(), 2u);
		const auto length = packet.ReadVarInt();
		ASSERT_TRUE(length.has_value());
		ASSERT_GT(*length, 0);
		const auto raw = packet.ReadBytes(static_cast<std::size_t>(*length));
		ASSERT_TRUE(raw.has_value());
		ByteReader sections(*raw);
		for (std::size_t index = 0; index < 24; ++index) {
			/* A second short would shift both palette containers. */
			ASSERT_EQ(*sections.ReadInt16(), 0);
			const auto blocks = ReadContainer(sections, 4096);
			const auto biomes = ReadContainer(sections, 64);
			EXPECT_EQ(blocks.Bits, 0u);
			EXPECT_EQ(blocks.Palette, (std::vector<std::uint32_t>{0}));
			EXPECT_EQ(biomes.Bits, 0u);
			EXPECT_EQ(biomes.Palette, (std::vector<std::uint32_t>{0}));
		}
		EXPECT_TRUE(sections.Empty());
		EXPECT_EQ(*packet.ReadVarInt(), 0); // no block entities
		const std::uint64_t all_light = (std::uint64_t{1} << 26) - 1u;
		EXPECT_EQ(ReadBitSet(packet), all_light);
		EXPECT_EQ(ReadBitSet(packet), 0u);
		EXPECT_EQ(ReadBitSet(packet), 0u);
		EXPECT_EQ(ReadBitSet(packet), all_light);
		ASSERT_EQ(*packet.ReadVarInt(), 26);
		for (int index = 0; index < 26; ++index) {
			ASSERT_EQ(*packet.ReadVarInt(), 2048);
			const auto bytes = packet.ReadBytes(2048);
			ASSERT_TRUE(bytes.has_value());
			for (const auto value : *bytes) EXPECT_EQ(value, std::byte{0xFF});
		}
		EXPECT_EQ(*packet.ReadVarInt(), 0);
		EXPECT_TRUE(packet.Empty());
	}

	TEST(ChunkDeliveryTests, EncodesProtocol774FirstLightColumnExactly) {
		const auto snapshot =
			MakeSnapshot();

		PlayChunkEncodingDiagnostics diagnostics;

		const auto encoded =
			EncodeLevelChunkWithLight(
				MakeColumn(),
				*snapshot,
				&diagnostics);

		ASSERT_TRUE(encoded.has_value());

		EXPECT_EQ(
			encoded->PacketId,
			Aurore::Protocol::Packets::Play::
				Clientbound::
				LevelChunkWithLightPacketId);

		ByteReader packet(
			encoded->Payload);

		ASSERT_EQ(
			*packet.ReadInt32(),
			0);
		ASSERT_EQ(
			*packet.ReadInt32(),
			0);

		const auto heightmaps =
			ReadHeightmaps(
				packet,
				9);

		ASSERT_EQ(
			heightmaps.size(),
			2u);

		EXPECT_EQ(
			heightmaps[0].Type,
			static_cast<std::int32_t>(
				PlayHeightmapType::
					WorldSurface));

		EXPECT_EQ(
			heightmaps[1].Type,
			static_cast<std::int32_t>(
				PlayHeightmapType::
					MotionBlocking));

		for (const auto& heightmap :
			heightmaps) {

			EXPECT_EQ(
				heightmap.Longs.size(),
				37u);

			ExpectFirstLightHeightmap(
				heightmap.Values);
		}

		const auto data_size =
			packet.ReadVarInt();

		ASSERT_TRUE(
			data_size.has_value());

		ASSERT_GT(
			*data_size,
			0);

		const auto data =
			packet.ReadBytes(
				static_cast<std::size_t>(
					*data_size));

		ASSERT_TRUE(data.has_value());

		ByteReader sections(*data);

		for (std::size_t section{ 0 };
			section < 24;
			++section) {

			const auto count =
				sections.ReadInt16();

			ASSERT_TRUE(
				count.has_value());

			const auto blocks =
				ReadContainer(
					sections,
					4096);

			const auto biomes =
				ReadContainer(
					sections,
					64);

			ASSERT_EQ(
				biomes.Palette.size(),
				1u);

			EXPECT_EQ(
				biomes.Palette.front(),
				0u);

			if (section != 7) {
				EXPECT_EQ(
					*count,
					0);

				ASSERT_EQ(
					blocks.Palette.size(),
					1u);

				EXPECT_EQ(
					blocks.Palette.front(),
					0u);

				EXPECT_EQ(
					CountValue(
						blocks.Values,
						0u),
					4096u);

				continue;
			}

			EXPECT_EQ(
				*count,
				75);

			EXPECT_EQ(
				blocks.Bits,
				4);

			EXPECT_EQ(
				blocks.Palette,
				(std::vector<
					std::uint32_t>{
						0, 1, 2, 3 }));

			EXPECT_EQ(
				CountValue(
					blocks.Values,
					0u),
				4021u);

			EXPECT_EQ(
				CountValue(
					blocks.Values,
					1u),
				25u);

			EXPECT_EQ(
				CountValue(
					blocks.Values,
					2u),
				25u);

			EXPECT_EQ(
				CountValue(
					blocks.Values,
					3u),
				25u);

			const auto at =
				[&blocks](
					std::uint8_t x,
					std::uint8_t y,
					std::uint8_t z) {

				return blocks.Values[
					(static_cast<
						std::size_t>(y)
						* 256u)
					+ (static_cast<
						std::size_t>(z)
						* 16u)
					+ x];
			};

			for (std::uint8_t z{ 0 };
				z < 5;
				++z) {

				for (std::uint8_t x{ 0 };
					x < 5;
					++x) {

					EXPECT_EQ(
						at(x, 13, z),
						1u);

					EXPECT_EQ(
						at(x, 14, z),
						2u);

					EXPECT_EQ(
						at(x, 15, z),
						3u);
				}
			}

			EXPECT_EQ(
				at(5, 13, 0),
				0u);

			EXPECT_EQ(
				at(0, 13, 5),
				0u);

			EXPECT_EQ(
				at(15, 15, 15),
				0u);
		}

		EXPECT_TRUE(
			sections.Empty());

		ASSERT_EQ(
			*packet.ReadVarInt(),
			0);

		constexpr std::uint64_t
			AllLightSections =
				(std::uint64_t{ 1 } << 26)
				- 1u;

		EXPECT_EQ(
			ReadBitSet(packet),
			AllLightSections);

		EXPECT_EQ(
			ReadBitSet(packet),
			0u);

		EXPECT_EQ(
			ReadBitSet(packet),
			0u);

		EXPECT_EQ(
			ReadBitSet(packet),
			AllLightSections);

		ASSERT_EQ(
			*packet.ReadVarInt(),
			26);

		for (int section{ 0 };
			section < 26;
			++section) {

			ASSERT_EQ(
				*packet.ReadVarInt(),
				2048);

			const auto light =
				packet.ReadBytes(2048);

			ASSERT_TRUE(
				light.has_value());

			for (const auto byte :
				*light)
				ASSERT_EQ(
					byte,
					std::byte{ 0xFF });
		}

		ASSERT_EQ(
			*packet.ReadVarInt(),
			0);

		EXPECT_TRUE(
			packet.Empty());

		EXPECT_EQ(
			diagnostics.TotalNonAirBlocks,
			75u);
	}

	TEST(ChunkDeliveryTests, AuditsFirstLightLightPayloadContract) {
		const auto snapshot = MakeSnapshot();

		PlayChunkEncodingDiagnostics diagnostics;
		const auto encoded = EncodeLevelChunkWithLight(
			MakeColumn(),
			*snapshot,
			&diagnostics);

		ASSERT_TRUE(encoded.has_value());

		const auto& light = diagnostics.Light;
		ASSERT_EQ(light.LightSectionCount, 26u);

		const std::uint64_t expected_mask =
			(std::uint64_t{ 1 } << 26) - 1u;

		EXPECT_EQ(light.SkyLightMask, expected_mask);
		EXPECT_EQ(light.BlockLightMask, 0u);
		EXPECT_EQ(light.EmptySkyLightMask, 0u);
		EXPECT_EQ(light.EmptyBlockLightMask, expected_mask);

		EXPECT_EQ(light.SkyLightMask & light.EmptySkyLightMask, 0u);
		EXPECT_EQ(light.BlockLightMask & light.EmptyBlockLightMask, 0u);

		EXPECT_EQ(
			light.SkyLightMask | light.EmptySkyLightMask,
			expected_mask);
		EXPECT_EQ(
			light.BlockLightMask | light.EmptyBlockLightMask,
			expected_mask);

		EXPECT_EQ(light.SkyLightArrayCount, 26u);
		EXPECT_EQ(light.BlockLightArrayCount, 0u);
		EXPECT_EQ(light.BytesPerLightArray, 2048u);

		EXPECT_NE(light.SkyLightMask & (std::uint64_t{ 1 } << 0), 0u);
		EXPECT_NE(light.SkyLightMask & (std::uint64_t{ 1 } << 1), 0u);
		EXPECT_NE(light.SkyLightMask & (std::uint64_t{ 1 } << 24), 0u);
		EXPECT_NE(light.SkyLightMask & (std::uint64_t{ 1 } << 25), 0u);
	}

	TEST(ChunkDeliveryTests, RejectsUnknownWorldBlock) {
		auto column =
			MakeColumn();

		column.Blocks.push_back(
			PlayChunkBlock{
				.X = 8,
				.Y = 63,
				.Z = 8,
				.Block =
					Location(
						"minecraft:definitely_missing"),
			});

		const auto result =
			EncodeLevelChunkWithLight(
				column,
				*MakeSnapshot());

		EXPECT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error(),
			Aurore::Protocol::
				ProtocolError::
					PlayBootstrapFailed);
	}

	TEST(ChunkDeliveryFullCorpusTests, ResolvesMinecraft12111FirstLightStatesWhenConfigured) {
		const char* root_value =
			std::getenv(
				"AURORE_TEST_MINECRAFT_DATA_ROOT");

		if (root_value == nullptr
			|| *root_value == '\0') {

			GTEST_SKIP()
				<< "Set AURORE_TEST_MINECRAFT_DATA_ROOT "
				   "to the generated Minecraft 1.21.11 corpus root.";
		}

		const std::filesystem::path root{
			root_value
		};

		const auto corpus =
			MinecraftDataCorpusFactory::
				FromPreparation(
					RawDataPreparationResult{
						.Version = {
							.Id = "1.21.11",
							.ProtocolVersion = 774,
						},
						.SourceSha1 =
							"0123456789abcdef0123456789abcdef01234567",
						.GenerationRoot = root,
						.ReportsRoot =
							root / "reports",
						.DataRoot =
							root / "data",
					});

		ASSERT_TRUE(
			corpus.has_value());

		const auto snapshot =
			VanillaDataImporter::Import(
				*corpus,
				1);

		ASSERT_TRUE(
			snapshot.has_value());

		ASSERT_NE(
			*snapshot,
			nullptr);

		PlayChunkEncodingDiagnostics
			diagnostics;

		const auto encoded =
			EncodeLevelChunkWithLight(
				MakeColumn(),
				**snapshot,
				&diagnostics);

		ASSERT_TRUE(
			encoded.has_value());

		const auto* air =
			FindResolved(
				diagnostics,
				"minecraft:air");

		const auto* stone =
			FindResolved(
				diagnostics,
				"minecraft:stone");

		const auto* dirt =
			FindResolved(
				diagnostics,
				"minecraft:dirt");

		const auto* grass =
			FindResolved(
				diagnostics,
				"minecraft:grass_block");

		ASSERT_NE(air, nullptr);
		ASSERT_NE(stone, nullptr);
		ASSERT_NE(dirt, nullptr);
		ASSERT_NE(grass, nullptr);

		std::cout
			<< "[First Light] Minecraft 1.21.11 block states: "
			<< "air=" << air->StateId
			<< ", stone=" << stone->StateId
			<< ", dirt=" << dirt->StateId
			<< ", grass_block=" << grass->StateId
			<< '\n';

		EXPECT_EQ(
			air->StateId,
			0u);

		EXPECT_NE(
			stone->StateId,
			air->StateId);

		EXPECT_NE(
			dirt->StateId,
			air->StateId);

		EXPECT_NE(
			grass->StateId,
			air->StateId);

		EXPECT_NE(
			stone->StateId,
			dirt->StateId);

		EXPECT_NE(
			stone->StateId,
			grass->StateId);

		EXPECT_NE(
			dirt->StateId,
			grass->StateId);

		EXPECT_EQ(
			diagnostics.TotalNonAirBlocks,
			75u);

		ASSERT_EQ(
			diagnostics.Sections.size(),
			24u);

		const auto& platform =
			diagnostics.Sections[7];

		EXPECT_EQ(
			platform.MinimumY,
			48);

		EXPECT_EQ(
			platform.MaximumY,
			63);

		EXPECT_EQ(
			platform.NonAirBlocks,
			75);

		EXPECT_EQ(
			platform.BitsPerBlock,
			4u);

		ASSERT_EQ(
			platform.Palette.size(),
			4u);

		EXPECT_EQ(
			platform.Palette[0],
			air->StateId);

		EXPECT_EQ(
			platform.Palette[1],
			stone->StateId);

		EXPECT_EQ(
			platform.Palette[2],
			dirt->StateId);

		EXPECT_EQ(
			platform.Palette[3],
			grass->StateId);

		ASSERT_EQ(
			diagnostics.Heightmaps.size(),
			2u);

		for (const auto& heightmap :
			diagnostics.Heightmaps) {

			EXPECT_EQ(
				heightmap.BitsPerEntry,
				9u);

			EXPECT_EQ(
				heightmap.PackedLongs.size(),
				37u);

			ExpectFirstLightHeightmap(
				heightmap.Values);
		}
	}
}
