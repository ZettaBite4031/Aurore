#include <Aurore/Protocol/PlayChunk.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iterator>
#include <span>
#include <vector>

namespace {
	using Aurore::Protocol::PlayChunkBlock;
	using Aurore::Protocol::PlayChunkColumn;
	using Aurore::Protocol::PlayChunkEncodingDiagnostics;
	using Aurore::Protocol::PlayChunkSectionDiagnostic;
	using Aurore::Protocol::PlayHeightmapDiagnostic;
	using Aurore::Protocol::PlayHeightmapType;
	using Aurore::Protocol::PlayLightDiagnostic;
	using Aurore::Protocol::ResolvedBlockStateDiagnostic;
	using Aurore::Util::ByteBuffer;
	using Aurore::Util::RegistryRuntimeId;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::ResourceLocation;

	constexpr std::size_t BlocksPerSection{ 16u * 16u * 16u };
	constexpr std::size_t HeightmapEntries{ 16u * 16u };
	constexpr std::size_t LightBytesPerSection{ 2048u };

	struct BlockContainerDiagnostic final {
		std::uint8_t Bits{ 0 };
		std::vector<std::uint32_t> Palette;
	};

	[[nodiscard]] std::uint8_t RequiredBits(std::size_t values) noexcept {
		if (values <= 1) return 0;
		return static_cast<std::uint8_t>(std::bit_width(values - 1));
	}

	void WriteBitSet(ByteBuffer& buffer, std::uint64_t mask) {
		if (mask == 0) {
			buffer.WriteVarInt(0);
			return;
		}

		buffer.WriteVarInt(1);
		buffer.WriteInt64(static_cast<std::int64_t>(mask));
	}

	[[nodiscard]] std::vector<std::int64_t> PackValues(std::span<const std::uint32_t> values, std::uint8_t bits) {
		std::vector<std::int64_t> result;
		if (bits == 0 || values.empty())
			return result;

		const auto values_per_long = static_cast<std::size_t>(64u / bits);
		const auto long_count = (values.size() + values_per_long - 1u) / values_per_long;

		result.reserve(long_count);
		const auto mask = bits == 32 ? static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) : ((std::uint64_t{ 1 } << bits) - 1u);
		for (std::size_t offset{ 0 }; offset < values.size(); offset += values_per_long) {
			std::uint64_t packed{ 0 };

			const auto count = std::min(values_per_long, values.size() - offset);
			for (std::size_t index{ 0 }; index < count; ++index) 
				packed |= (static_cast<std::uint64_t>(values[offset + index]) & mask) << (index * bits);

			result.push_back(static_cast<std::int64_t>(packed));
		}

		return result;
	}

	void WritePackedValues(ByteBuffer& buffer, std::span<const std::uint32_t> values, std::uint8_t bits) {
		for (const auto value : PackValues(values, bits)) buffer.WriteInt64(value);
	}

	[[nodiscard]] bool WriteBlockContainer(ByteBuffer& buffer, std::span<const std::uint32_t> states, BlockContainerDiagnostic* diagnostic) {
		std::vector<std::uint32_t> palette;
		palette.reserve(16);

		std::vector<std::uint32_t> indices;
		indices.reserve(states.size());

		for (const auto state : states) {
			auto iterator = std::find(palette.begin(), palette.end(), state);
			if (iterator == palette.end()) {
				if (palette.size() >= 256)
					return false;

				palette.push_back(state);
				iterator = std::prev(palette.end());
			}

			indices.push_back(static_cast<std::uint32_t>(std::distance(palette.begin(), iterator)));
		}

		if (palette.size() == 1) {
			buffer.WriteUnsignedByte(0);
			buffer.WriteVarInt(static_cast<std::int32_t>(palette.front()));

			if (diagnostic != nullptr) {
				diagnostic->Bits = 0;
				diagnostic->Palette = palette;
			}

			return true;
		}

		const auto bits = std::max<std::uint8_t>(4, RequiredBits(palette.size()));
		if (bits > 8) return false;

		buffer.WriteUnsignedByte(bits);
		buffer.WriteVarInt(static_cast<std::int32_t>(palette.size()));

		for (const auto state : palette)
			buffer.WriteVarInt(static_cast<std::int32_t>(state));

		WritePackedValues(buffer, indices, bits);

		if (diagnostic != nullptr) {
			diagnostic->Bits = bits;
			diagnostic->Palette = std::move(palette);
		}

		return true;
	}

	void WriteSingleBiomeContainer(ByteBuffer& buffer, RegistryRuntimeId biome) {
		buffer.WriteUnsignedByte(0);
		buffer.WriteVarInt(static_cast<std::int32_t>(biome));
	}

	[[nodiscard]] const Aurore::Util::BlockRegistry::Entry* FindBlock(const RegistrySnapshot& registries, const ResourceLocation& key) noexcept {
		return registries.GetBlocks().Find(key);
	}

	[[nodiscard]] std::size_t SectionIndex(std::int32_t minimum_y, std::int32_t y) noexcept {
		return static_cast<std::size_t>((y - minimum_y) / 16);
	}

	[[nodiscard]] std::size_t BlockIndex(std::uint8_t x, std::int32_t local_y, std::uint8_t z) noexcept {
		return static_cast<std::size_t>((local_y * 256) + (static_cast<std::int32_t>(z) * 16) + x);
	}

	[[nodiscard]] std::size_t HeightmapIndex(std::uint8_t x, std::uint8_t z) noexcept {
		return static_cast<std::size_t>(z) * 16u + static_cast<std::size_t>(x);
	}

	void RecordResolvedState(PlayChunkEncodingDiagnostics* diagnostics, const ResourceLocation& block, std::uint32_t state_id) {
		if (diagnostics == nullptr) return;

		const auto existing = std::find_if(diagnostics->ResolvedBlockStates.begin(), diagnostics->ResolvedBlockStates.end(), [&block]( const ResolvedBlockStateDiagnostic& entry) { return entry.Block == block; });
		if (existing != diagnostics->ResolvedBlockStates.end()) return;

		diagnostics->ResolvedBlockStates.push_back(ResolvedBlockStateDiagnostic{ .Block = block, .StateId = state_id, });
	}

	[[nodiscard]] std::vector<std::uint32_t> BuildHeightmapValues(const PlayChunkColumn& chunk, std::uint32_t air_state, const std::vector<std::array<std::uint32_t, BlocksPerSection>>& sections) {
		std::vector<std::uint32_t> values(HeightmapEntries, 0);

		const auto maximum_y = chunk.MinimumY + chunk.Height - 1;
		for (std::uint8_t z{ 0 }; z < 16; ++z) {
			for (std::uint8_t x{ 0 }; x < 16; ++x) {
				std::uint32_t normalized_height{ 0 };
				for (std::int32_t y = maximum_y; y >= chunk.MinimumY; --y) {
					const auto section = SectionIndex(chunk.MinimumY, y);
					const auto local_y = (y - chunk.MinimumY) & 15;
					const auto index = BlockIndex(x, local_y, z);
					if (sections[section][index] == air_state) continue;

					/*
						Heightmaps store the first available Y above
						the matching surface, normalized against the
						dimension's minimum Y. A block at Y=63 in a
						minY=-64 world therefore stores 128.
					*/
					normalized_height = static_cast<std::uint32_t>((y + 1) - chunk.MinimumY);
					break;
				}

				values[HeightmapIndex(x, z)] = normalized_height;
			}
		}

		return values;
	}

	void WriteHeightmap(ByteBuffer& payload, PlayHeightmapType type, std::span<const std::uint32_t> values, std::uint8_t bits, PlayChunkEncodingDiagnostics* diagnostics) {
		const auto packed = PackValues(values, bits);
		payload.WriteVarInt(static_cast<std::int32_t>(type));
		payload.WriteVarInt(static_cast<std::int32_t>(packed.size()));
		for (const auto value : packed) payload.WriteInt64(value);
		if (diagnostics != nullptr) {
			diagnostics->Heightmaps.push_back(
				PlayHeightmapDiagnostic{
					.Type = type,
					.BitsPerEntry = bits,
					.Values = std::vector<std::uint32_t>(values.begin(), values.end()),
					.PackedLongs = packed,
				}
			);
		}
	}
}

namespace Aurore::Protocol {
	ProtocolResult<PacketFrame> EncodeLevelChunkWithLight(const PlayChunkColumn& chunk, const RegistrySnapshot& registries, PlayChunkEncodingDiagnostics* diagnostics) {
		if (diagnostics != nullptr) *diagnostics = PlayChunkEncodingDiagnostics{};
		if (chunk.Height <= 0 || (chunk.Height % 16) != 0)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto section_count = static_cast<std::size_t>(chunk.Height / 16);
		const auto light_section_count = section_count + 2;
		if (light_section_count > 64)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto heightmap_bits = RequiredBits(static_cast<std::size_t>(chunk.Height) + 1u);
		if (heightmap_bits == 0 || heightmap_bits > 32)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto* air = FindBlock(registries, chunk.AirBlock);
		const auto* biome = registries.GetBiomes().Find(chunk.Biome);
		if (air == nullptr || biome == nullptr)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto air_state =static_cast<std::uint32_t>(air->Value.DefaultStateId);
		if (diagnostics != nullptr) {
			diagnostics->AirStateId = air_state;
			diagnostics->BiomeRuntimeId = biome->RuntimeId;
			diagnostics->Sections.reserve(section_count);
			diagnostics->Heightmaps.reserve(2);
		}

		RecordResolvedState(diagnostics, chunk.AirBlock, air_state);
		std::vector<std::array<std::uint32_t, BlocksPerSection>> sections(section_count);
		std::vector<std::int16_t> non_air(section_count, 0);
		for (auto& section : sections) section.fill(air_state);
		for (const PlayChunkBlock& block : chunk.Blocks) {
			if (block.X >= 16 || block.Z >= 16 || block.Y < chunk.MinimumY || block.Y >= chunk.MinimumY + chunk.Height)
				return std::unexpected(ProtocolError::PlayBootstrapFailed);

			const auto* definition = FindBlock(registries, block.Block);
			if (definition == nullptr)
				return std::unexpected(ProtocolError::PlayBootstrapFailed);

			const auto next = static_cast<std::uint32_t>(definition->Value.DefaultStateId);
			RecordResolvedState(diagnostics, block.Block, next);

			const auto section = SectionIndex(chunk.MinimumY, block.Y);
			const auto local_y = (block.Y - chunk.MinimumY) & 15;
			const auto index = BlockIndex(block.X, local_y, block.Z);
			const auto previous = sections[section][index];

			if (previous == air_state && next != air_state) ++non_air[section];
			else if (previous != air_state && next == air_state) --non_air[section];

			sections[section][index] = next;
		}

		const auto heightmap_values = BuildHeightmapValues(chunk, air_state, sections);

		ByteBuffer chunk_data;
		for (std::size_t index{ 0 }; index < section_count; ++index) {
			chunk_data.WriteInt16(non_air[index]);

			BlockContainerDiagnostic container_diagnostic;

			if (!WriteBlockContainer(chunk_data, sections[index], diagnostics != nullptr ? &container_diagnostic : nullptr)) 
				return std::unexpected(ProtocolError::PlayBootstrapFailed);

			WriteSingleBiomeContainer(chunk_data, biome->RuntimeId);

			if (diagnostics != nullptr) {
				const auto section_minimum_y = chunk.MinimumY + static_cast<std::int32_t>(index * 16);
				diagnostics->TotalNonAirBlocks += static_cast<std::size_t>(non_air[index]);
				diagnostics->Sections.push_back(
					PlayChunkSectionDiagnostic{
						.Index = index,
						.MinimumY = section_minimum_y,
						.MaximumY = section_minimum_y + 15,
						.NonAirBlocks = non_air[index],
						.BitsPerBlock = container_diagnostic.Bits,
						.Palette = std::move(container_diagnostic.Palette),
					}
				);
			}
		}

		ByteBuffer payload;

		payload.WriteInt32(chunk.X);
		payload.WriteInt32(chunk.Z);

		/*
			Protocol 774 represents heightmaps as a prefixed
			array of (type, long[]) entries.
		*/
		payload.WriteVarInt(2);

		WriteHeightmap(payload, PlayHeightmapType::WorldSurface, heightmap_values, heightmap_bits, diagnostics);
		WriteHeightmap(payload, PlayHeightmapType::MotionBlocking, heightmap_values, heightmap_bits, diagnostics);

		payload.WriteVarInt(static_cast<std::int32_t>(chunk_data.Size()));
		payload.WriteBytes(chunk_data.Bytes());

		payload.WriteVarInt(0);

		const std::uint64_t all_light_sections = light_section_count == 64 ? std::numeric_limits<std::uint64_t>::max() : ((std::uint64_t{ 1 } << light_section_count) - 1u);

		/*
			Light section indices include one section below the dimension and
			one section above it. First Light's overworld therefore has:
			  bit 0      : below minY
			  bits 1..24 : the 24 real chunk sections
			  bit 25     : above maxY

			For the current void world we explicitly provide full skylight for
			all 26 light sections and declare all block-light sections empty.
		*/
		const std::uint64_t sky_light_mask = all_light_sections;
		const std::uint64_t block_light_mask = 0;
		const std::uint64_t empty_sky_light_mask = 0;
		const std::uint64_t empty_block_light_mask = all_light_sections;

		WriteBitSet(payload, sky_light_mask);
		WriteBitSet(payload, block_light_mask);
		WriteBitSet(payload, empty_sky_light_mask);
		WriteBitSet(payload, empty_block_light_mask);

		payload.WriteVarInt(static_cast<std::int32_t>(light_section_count));

		std::array<std::byte, LightBytesPerSection> full_sky{};
		full_sky.fill(std::byte{ 0xFF });

		for (std::size_t index{ 0 }; index < light_section_count; ++index) {
			payload.WriteVarInt(static_cast<std::int32_t>(full_sky.size()));
			payload.WriteBytes(std::span<const std::byte>{ full_sky });
		}

		/* No block-light arrays follow because block_light_mask is empty. */
		payload.WriteVarInt(0);

		if (diagnostics != nullptr) {
			diagnostics->Light = PlayLightDiagnostic{
				.LightSectionCount = light_section_count,
				.SkyLightMask = sky_light_mask,
				.BlockLightMask = block_light_mask,
				.EmptySkyLightMask = empty_sky_light_mask,
				.EmptyBlockLightMask = empty_block_light_mask,
				.SkyLightArrayCount = light_section_count,
				.BlockLightArrayCount = 0,
				.BytesPerLightArray = LightBytesPerSection,
			};
		}

		return PacketFrame{
			.PacketId = Packets::Play::Clientbound::LevelChunkWithLightPacketId,
			.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end()),
		};
	}
}
