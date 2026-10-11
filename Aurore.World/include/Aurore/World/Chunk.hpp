#pragma once

#include <Aurore/Util/ResourceLocation.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace Aurore::World {
	struct ChunkPosition final {
		std::int32_t X{ 0 };
		std::int32_t Z{ 0 };

		auto operator<=>(const ChunkPosition&) const noexcept = default;
	};

	struct LocalBlockPosition final {
		std::uint8_t X{ 0 };
		std::int32_t Y{ 0 };
		std::uint8_t Z{ 0 };

		auto operator<=>(const LocalBlockPosition&) const noexcept = default;
	};

	struct BlockPosition final {
		std::int32_t X{ 0 };
		std::int32_t Y{ 0 };
		std::int32_t Z{ 0 };

		auto operator<=>(const BlockPosition&) const noexcept = default;
	};

	class Chunk final {
	public:
		static constexpr std::int32_t Width{ 16 };

		Chunk(ChunkPosition position,
			std::int32_t minimum_y,
			std::int32_t height,
			Aurore::Util::ResourceLocation air_block);

		Chunk(const Chunk&) = delete;
		Chunk& operator=(const Chunk&) = delete;
		Chunk(Chunk&&) noexcept = default;
		Chunk& operator=(Chunk&&) noexcept = default;

		[[nodiscard]] bool SetBlock(LocalBlockPosition position,
			Aurore::Util::ResourceLocation block);

		[[nodiscard]] const Aurore::Util::ResourceLocation&
			GetBlock(LocalBlockPosition position) const noexcept;

		[[nodiscard]] bool ContainsY(std::int32_t y) const noexcept;
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t StoredBlockCount() const noexcept;
		[[nodiscard]] ChunkPosition GetPosition() const noexcept;
		[[nodiscard]] std::int32_t GetMinimumY() const noexcept;
		[[nodiscard]] std::int32_t GetHeight() const noexcept;
		[[nodiscard]] const Aurore::Util::ResourceLocation& GetAirBlock() const noexcept;

	private:
		[[nodiscard]] static std::uint64_t Pack(LocalBlockPosition position) noexcept;
		[[nodiscard]] bool IsLocalCoordinateValid(LocalBlockPosition position) const noexcept;

		ChunkPosition m_Position;
		std::int32_t m_MinimumY{ 0 };
		std::int32_t m_Height{ 0 };
		Aurore::Util::ResourceLocation m_AirBlock;
		std::unordered_map<std::uint64_t, Aurore::Util::ResourceLocation> m_Blocks;
	};

	[[nodiscard]] ChunkPosition GetChunkPosition(BlockPosition position) noexcept;
	[[nodiscard]] LocalBlockPosition GetLocalBlockPosition(BlockPosition position) noexcept;
}

template<>
struct std::hash<Aurore::World::ChunkPosition> {
	std::size_t operator()(const Aurore::World::ChunkPosition& position) const noexcept {
		const auto x = std::hash<std::int32_t>{}(position.X);
		const auto z = std::hash<std::int32_t>{}(position.Z);
		return x ^ (z + static_cast<std::size_t>(0x9e3779b9u) + (x << 6) + (x >> 2));
	}
};
