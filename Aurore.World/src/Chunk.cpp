#include <Aurore/World/Chunk.hpp>

#include <cstdint>
#include <utility>

namespace {
	[[nodiscard]] std::int32_t FloorDiv16(std::int32_t value) noexcept {
		if (value >= 0) return value / 16;
		return -(((-value) + 15) / 16);
	}

	[[nodiscard]] std::uint8_t FloorMod16(std::int32_t value) noexcept {
		const auto chunk = FloorDiv16(value);
		return static_cast<std::uint8_t>(value - (chunk * 16));
	}
}

namespace Aurore::World {
	Chunk::Chunk(ChunkPosition position, std::int32_t minimum_y, std::int32_t height, Aurore::Util::ResourceLocation air_block)
		: m_Position(position), m_MinimumY(minimum_y), m_Height(height), m_AirBlock(std::move(air_block)) {}

	bool Chunk::SetBlock(LocalBlockPosition position, Aurore::Util::ResourceLocation block) {
		if (!IsLocalCoordinateValid(position)) return false;

		const auto key = Pack(position);
		if (block == m_AirBlock) {
			m_Blocks.erase(key);
			return true;
		}

		m_Blocks.insert_or_assign(key, std::move(block));
		return true;
	}

	const Aurore::Util::ResourceLocation& Chunk::GetBlock(LocalBlockPosition position) const noexcept {
		if (!IsLocalCoordinateValid(position)) return m_AirBlock;
		const auto iterator = m_Blocks.find(Pack(position));
		return iterator == m_Blocks.end() ? m_AirBlock : iterator->second;
	}

	bool Chunk::ContainsY(std::int32_t y) const noexcept {
		return m_Height > 0 && y >= m_MinimumY && y < m_MinimumY + m_Height;
	}

	bool Chunk::Empty() const noexcept { return m_Blocks.empty(); }
	std::size_t Chunk::StoredBlockCount() const noexcept { return m_Blocks.size(); }

	std::vector<StoredBlock> Chunk::GetStoredBlocks() const {
		std::vector<StoredBlock> result;
		result.reserve(m_Blocks.size());

		for (const auto& [packed, block] : m_Blocks) {
			result.push_back(StoredBlock{
				.Position = LocalBlockPosition{
					.X = static_cast<std::uint8_t>(packed & 0x0Fu),
					.Y = static_cast<std::int32_t>(static_cast<std::uint32_t>(packed >> 8)),
					.Z = static_cast<std::uint8_t>((packed >> 4) & 0x0Fu),
				},
				.Block = block,
			});
		}

		return result;
	}

	ChunkPosition Chunk::GetPosition() const noexcept { return m_Position; }
	std::int32_t Chunk::GetMinimumY() const noexcept { return m_MinimumY; }
	std::int32_t Chunk::GetHeight() const noexcept { return m_Height; }
	const Aurore::Util::ResourceLocation& Chunk::GetAirBlock() const noexcept { return m_AirBlock; }

	std::uint64_t Chunk::Pack(LocalBlockPosition position) noexcept {
		const auto x = static_cast<std::uint64_t>(position.X);
		const auto z = static_cast<std::uint64_t>(position.Z);
		const auto y = static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.Y));
		return (y << 8) | (z << 4) | x;
	}

	bool Chunk::IsLocalCoordinateValid(LocalBlockPosition position) const noexcept {
		return position.X < Width && position.Z < Width && ContainsY(position.Y);
	}

	ChunkPosition GetChunkPosition(BlockPosition position) noexcept {
		return ChunkPosition{
			.X = FloorDiv16(position.X),
			.Z = FloorDiv16(position.Z),
		};
	}

	LocalBlockPosition GetLocalBlockPosition(BlockPosition position) noexcept {
		return LocalBlockPosition{
			.X = FloorMod16(position.X),
			.Y = position.Y,
			.Z = FloorMod16(position.Z),
		};
	}
}
