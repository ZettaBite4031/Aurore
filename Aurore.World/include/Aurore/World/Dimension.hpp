#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Aurore::World {
	struct WorldTickContext final {
		std::uint64_t TickNumber{ 0 };
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

		[[nodiscard]] DimensionKind GetKind() const noexcept;
		[[nodiscard]] std::string_view GetIdentifier() const noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
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

	private:
		DimensionKind m_Kind;
		std::string m_Identifier;

		bool m_Initialized{ false };
	};
}
