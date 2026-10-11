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
#include <unordered_map>
#include <vector>

namespace {
	using Aurore::Protocol::PlayChunkBlock;
	using Aurore::Protocol::PlayChunkColumn;
	using Aurore::Util::ByteBuffer;
	using Aurore::Util::RegistryRuntimeId;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::ResourceLocation;

	constexpr std::size_t BlocksPerSection{ 16u * 16u * 16u };
	constexpr std::size_t BiomesPerSection{ 4u * 4u * 4u };
	constexpr std::size_t LightBytesPerSection{ 2048u };

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

	void WritePackedValues(ByteBuffer& buffer, std::span<const std::uint32_t> values, std::uint8_t bits) {
		const auto values_per_long = static_cast<std::size_t>(64u / bits);
		const auto mask = bits == 32 ? std::numeric_limits<std::uint32_t>::max() : ((std::uint64_t{ 1 } << bits) - 1u);

		for (std::size_t offset{ 0 }; offset < values.size(); offset += values_per_long) {
			std::uint64_t packed{ 0 };
			const auto count = std::min(values_per_long, values.size() - offset);
			for (std::size_t index{ 0 }; index < count; ++index)
				packed |= (static_cast<std::uint64_t>(values[offset + index]) & mask) << (index * bits);
			buffer.WriteInt64(static_cast<std::int64_t>(packed));
		}
	}

	[[nodiscard]] bool WriteBlockContainer(ByteBuffer& buffer, std::span<const std::uint32_t> states) {
		std::vector<std::uint32_t> palette;
		palette.reserve(16);
		std::vector<std::uint32_t> indices;
		indices.reserve(states.size());

		for (const auto state : states) {
			auto iterator = std::find(palette.begin(), palette.end(), state);
			if (iterator == palette.end()) {
				if (palette.size() >= 256) return false;
				palette.push_back(state);
				iterator = std::prev(palette.end());
			}
			indices.push_back(static_cast<std::uint32_t>(std::distance(palette.begin(), iterator)));
		}

		if (palette.size() == 1) {
			buffer.WriteUnsignedByte(0);
			buffer.WriteVarInt(static_cast<std::int32_t>(palette.front()));
			return true;
		}

		const auto bits = std::max<std::uint8_t>(4, RequiredBits(palette.size()));
		if (bits > 8) return false; // First Light never needs the direct palette form.

		buffer.WriteUnsignedByte(bits);
		buffer.WriteVarInt(static_cast<std::int32_t>(palette.size()));
		for (const auto state : palette)
			buffer.WriteVarInt(static_cast<std::int32_t>(state));
		WritePackedValues(buffer, indices, bits);
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
}

namespace Aurore::Protocol {
	ProtocolResult<PacketFrame> EncodeLevelChunkWithLight(const PlayChunkColumn& chunk, const RegistrySnapshot& registries) {
		if (chunk.Height <= 0 || (chunk.Height % 16) != 0)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto section_count = static_cast<std::size_t>(chunk.Height / 16);
		const auto light_section_count = section_count + 2;
		if (light_section_count > 64)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		const auto* air = FindBlock(registries, chunk.AirBlock);
		const auto* biome = registries.GetBiomes().Find(chunk.Biome);
		if (air == nullptr || biome == nullptr)
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		std::vector<std::array<std::uint32_t, BlocksPerSection>> sections(section_count);
		std::vector<std::int16_t> non_air(section_count, 0);
		for (auto& section : sections)
			section.fill(air->Value.DefaultStateId);

		for (const PlayChunkBlock& block : chunk.Blocks) {
			if (block.X >= 16 || block.Z >= 16 || block.Y < chunk.MinimumY || block.Y >= chunk.MinimumY + chunk.Height)
				return std::unexpected(ProtocolError::PlayBootstrapFailed);

			const auto* definition = FindBlock(registries, block.Block);
			if (definition == nullptr)
				return std::unexpected(ProtocolError::PlayBootstrapFailed);

			const auto section = SectionIndex(chunk.MinimumY, block.Y);
			const auto local_y = (block.Y - chunk.MinimumY) & 15;
			const auto index = BlockIndex(block.X, local_y, block.Z);
			const auto previous = sections[section][index];
			const auto next = definition->Value.DefaultStateId;

			if (previous == air->Value.DefaultStateId && next != air->Value.DefaultStateId)
				++non_air[section];
			else if (previous != air->Value.DefaultStateId && next == air->Value.DefaultStateId)
				--non_air[section];

			sections[section][index] = next;
		}

		ByteBuffer chunk_data;
		for (std::size_t index{ 0 }; index < section_count; ++index) {
			chunk_data.WriteInt16(non_air[index]);
			if (!WriteBlockContainer(chunk_data, sections[index]))
				return std::unexpected(ProtocolError::PlayBootstrapFailed);
			WriteSingleBiomeContainer(chunk_data, biome->RuntimeId);
		}

		ByteBuffer payload;
		payload.WriteInt32(chunk.X);
		payload.WriteInt32(chunk.Z);

		/* Heightmap map. First Light intentionally sends no heightmaps. */
		payload.WriteVarInt(0);

		payload.WriteVarInt(static_cast<std::int32_t>(chunk_data.Size()));
		payload.WriteBytes(chunk_data.Bytes());

		/* No block entities exist in the First Light world. */
		payload.WriteVarInt(0);

		const std::uint64_t all_light_sections = light_section_count == 64
			? std::numeric_limits<std::uint64_t>::max()
			: ((std::uint64_t{ 1 } << light_section_count) - 1u);

		/*
			Sky present, block present, empty sky, empty block.
			The tiny void world is fully skylit and contains no block-light sources.
		*/
		WriteBitSet(payload, all_light_sections);
		WriteBitSet(payload, 0);
		WriteBitSet(payload, 0);
		WriteBitSet(payload, all_light_sections);

		payload.WriteVarInt(static_cast<std::int32_t>(light_section_count));
		std::array<std::byte, LightBytesPerSection> full_sky{};
		full_sky.fill(std::byte{ 0xFF });
		for (std::size_t index{ 0 }; index < light_section_count; ++index) {
			payload.WriteVarInt(static_cast<std::int32_t>(full_sky.size()));
			payload.WriteBytes(std::span<const std::byte>{ full_sky });
		}

		payload.WriteVarInt(0); // no block-light payloads

		return PacketFrame{
			.PacketId = Packets::Play::Clientbound::LevelChunkWithLightPacketId,
			.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end()),
		};
	}
}
