#pragma once

#include <Aurore/World/Chunk.hpp>

#include <Aurore/Util/ResourceLocation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Aurore::World {
	struct WorldTickContext final {
		std::uint64_t TickNumber{ 0 };
	};

	struct DimensionSpawnPosition final {
		double X{ 0.5 };
		double Y{ 64.0 };
		double Z{ 0.5 };
		float Yaw{ 0.0F };
		float Pitch{ 0.0F };

		bool operator==(const DimensionSpawnPosition&) const noexcept = default;
	};

	enum class DimensionKind : std::uint8_t {
		Overworld,
		Nether,
		End,
		Custom,
	};

	class Dimension final {
	public:
		Dimension(DimensionKind kind, std::string identifier);
		~Dimension() noexcept;

		Dimension(const Dimension&) = delete;
		Dimension& operator=(const Dimension&) = delete;
		Dimension(Dimension&&) = delete;
		Dimension& operator=(Dimension&&) = delete;

		[[nodiscard]] bool Initialize();
		void RunTick(const WorldTickContext& context);
		void Shutdown() noexcept;

		[[nodiscard]] bool SetBlock(BlockPosition position, Aurore::Util::ResourceLocation block);
		[[nodiscard]] const Aurore::Util::ResourceLocation& GetBlock(BlockPosition position) const noexcept;

		[[nodiscard]] Chunk* FindChunk(ChunkPosition position) noexcept;
		[[nodiscard]] const Chunk* FindChunk(ChunkPosition position) const noexcept;
		[[nodiscard]] std::size_t GetLoadedChunkCount() const noexcept;

		[[nodiscard]] DimensionKind GetKind() const noexcept;
		[[nodiscard]] std::string_view GetIdentifier() const noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] bool IsVoidWorld() const noexcept;
		[[nodiscard]] std::int32_t GetMinimumY() const noexcept;
		[[nodiscard]] std::int32_t GetHeight() const noexcept;
		[[nodiscard]] std::int32_t GetSeaLevel() const noexcept;
		[[nodiscard]] const DimensionSpawnPosition& GetSpawnPosition() const noexcept;
		[[nodiscard]] const Aurore::Util::ResourceLocation& GetAirBlock() const noexcept;

	private:
		[[nodiscard]] bool InitializeFirstLightOverworld();
		[[nodiscard]] Chunk* GetOrCreateChunk(ChunkPosition position);

		void UpdateWorldBorder(const WorldTickContext& context);
		void AdvanceWeatherCycle(const WorldTickContext& context);
		void AdvanceDaylightCycle(const WorldTickContext& context);
		void RunPlayerSleepingLogic(const WorldTickContext& context);
		void RunScheduledCommands(const WorldTickContext& context);
		void RunScheduledBlockTicks(const WorldTickContext& context);
		void RunScheduledFluidTicks(const WorldTickContext& context);
		void RunRaidLogic(const WorldTickContext& context);
		void UpdateChunkLoadLevels(const WorldTickContext& context);
		void TickChunksInRandomOrder(const WorldTickContext& context);
		void SendBlockChanges(const WorldTickContext& context);
		void UpdatePointsOfInterest(const WorldTickContext& context);
		void UnloadChunks(const WorldTickContext& context);
		void TickDragonFight(const WorldTickContext& context);
		void TickEntities(const WorldTickContext& context);
		void TickBlockEntities(const WorldTickContext& context);
		void HandleGameEvents(const WorldTickContext& context);

		DimensionKind m_Kind;
		std::string m_Identifier;
		bool m_VoidWorld{ true };
		std::int32_t m_MinimumY{ -64 };
		std::int32_t m_Height{ 384 };
		std::int32_t m_SeaLevel{ 63 };
		DimensionSpawnPosition m_SpawnPosition;
		Aurore::Util::ResourceLocation m_AirBlock;
		std::unordered_map<ChunkPosition, std::unique_ptr<Chunk>> m_Chunks;
		bool m_Initialized{ false };
	};
}
