#include <Aurore/World/Dimension.hpp>

#include <utility>

namespace {
	[[nodiscard]] Aurore::Util::ResourceLocation RequiredLocation(std::string_view value) {
		auto result = Aurore::Util::ResourceLocation::Parse(value);
		if (!result) std::terminate();
		return std::move(*result);
	}
}

namespace Aurore::World {
	Dimension::Dimension(DimensionKind kind, std::string identifier)
		: m_Kind(kind), m_Identifier(std::move(identifier)), m_AirBlock(RequiredLocation("minecraft:air")) {}

	Dimension::~Dimension() noexcept { Shutdown(); }

	bool Dimension::Initialize() {
		if (m_Initialized) return true;
		if (m_Identifier.empty()) return false;

		m_Chunks.clear();

		if (m_Kind == DimensionKind::Overworld
			&& !InitializeFirstLightOverworld()) {
			m_Chunks.clear();
			return false;
		}

		m_Initialized = true;
		return true;
	}

	void Dimension::RunTick(const WorldTickContext& context) {
		if (!m_Initialized) return;

		/*
			This ordering is compatibility-critical.

			Do not replace this sequence with a generic callback list,
			event bus, unordered system registry, or parallel job graph.
			The implementation of each phase may change, but its observable
			position in the dimension tick must remain faithful to vanilla.
		*/

		UpdateWorldBorder(context);
		AdvanceWeatherCycle(context);
		AdvanceDaylightCycle(context);
		RunPlayerSleepingLogic(context);
		RunScheduledCommands(context);
		RunScheduledBlockTicks(context);
		RunScheduledFluidTicks(context);
		RunRaidLogic(context);
		UpdateChunkLoadLevels(context);
		TickChunksInRandomOrder(context);
		SendBlockChanges(context);
		UpdatePointsOfInterest(context);
		UnloadChunks(context);
		TickDragonFight(context);
		TickEntities(context);
		TickBlockEntities(context);
		HandleGameEvents(context);
	}

	void Dimension::Shutdown() noexcept {
		if (!m_Initialized) return;

		/*
			Future subsystem shutdown belongs here.

			Shutdown should generally occur in reverse ownership order:

				- Stop accepting new world work
				- Complete or cancel pending operations
				- Persist required world state
				- Destroy simulation subsystems
		*/

		m_Chunks.clear();
		m_Initialized = false;
	}

	bool Dimension::SetBlock(BlockPosition position, Aurore::Util::ResourceLocation block) {
		if (position.Y < m_MinimumY || position.Y >= m_MinimumY + m_Height) return false;

		auto* chunk = GetOrCreateChunk(GetChunkPosition(position));
		if (chunk == nullptr) return false;

		return chunk->SetBlock(GetLocalBlockPosition(position), std::move(block));
	}

	const Aurore::Util::ResourceLocation& Dimension::GetBlock(BlockPosition position) const noexcept {
		if (position.Y < m_MinimumY || position.Y >= m_MinimumY + m_Height) return m_AirBlock;

		const auto* chunk = FindChunk(GetChunkPosition(position));
		if (chunk == nullptr) return m_AirBlock;

		return chunk->GetBlock(GetLocalBlockPosition(position));
	}

	Chunk* Dimension::FindChunk(ChunkPosition position) noexcept {
		const auto iterator = m_Chunks.find(position);
		return iterator == m_Chunks.end() ? nullptr : iterator->second.get();
	}

	const Chunk* Dimension::FindChunk(ChunkPosition position) const noexcept {
		const auto iterator = m_Chunks.find(position);
		return iterator == m_Chunks.end() ? nullptr : iterator->second.get();
	}

	std::size_t Dimension::GetLoadedChunkCount() const noexcept { return m_Chunks.size(); }
	DimensionKind Dimension::GetKind() const noexcept { return m_Kind; }
	std::string_view Dimension::GetIdentifier() const noexcept { return m_Identifier; }
	bool Dimension::IsInitialized() const noexcept { return m_Initialized; }
	bool Dimension::IsVoidWorld() const noexcept { return m_VoidWorld; }
	std::int32_t Dimension::GetMinimumY() const noexcept { return m_MinimumY; }
	std::int32_t Dimension::GetHeight() const noexcept { return m_Height; }
	std::int32_t Dimension::GetSeaLevel() const noexcept { return m_SeaLevel; }
	const DimensionSpawnPosition& Dimension::GetSpawnPosition() const noexcept { return m_SpawnPosition; }
	const Aurore::Util::ResourceLocation& Dimension::GetAirBlock() const noexcept { return m_AirBlock; }

	bool Dimension::InitializeFirstLightOverworld() {
		const auto grass = RequiredLocation("minecraft:grass_block");
		const auto dirt = RequiredLocation("minecraft:dirt");
		const auto stone = RequiredLocation("minecraft:stone");

		m_VoidWorld = true;
		m_MinimumY = -64;
		m_Height = 384;
		m_SeaLevel = 63;
		m_SpawnPosition = DimensionSpawnPosition{
			.X = 2.5,
			.Y = 64.0,
			.Z = 2.5,
			.Yaw = 0.0F,
			.Pitch = 0.0F,
		};

		/*
			First Light deliberately lives entirely inside chunk (0, 0):
			5x5 grass at Y=63, dirt at Y=62, stone at Y=61.

			Keeping the initial platform in one chunk makes Step 5 about correct
			chunk serialization rather than premature chunk-streaming policy.
		*/
		for (std::int32_t z{ 0 }; z < 5; ++z) {
			for (std::int32_t x{ 0 }; x < 5; ++x) {
				if (!SetBlock(BlockPosition{ .X = x, .Y = 61, .Z = z }, stone)) return false;
				if (!SetBlock(BlockPosition{ .X = x, .Y = 62, .Z = z }, dirt)) return false;
				if (!SetBlock(BlockPosition{ .X = x, .Y = 63, .Z = z }, grass)) return false;
			}
		}

		return true;
	}

	Chunk* Dimension::GetOrCreateChunk(ChunkPosition position) {
		if (auto* existing = FindChunk(position)) return existing;

		auto chunk = std::make_unique<Chunk>(position, m_MinimumY, m_Height, m_AirBlock);
		auto* result = chunk.get();
		const auto [iterator, inserted] = m_Chunks.emplace(position, std::move(chunk));
		return inserted ? result : iterator->second.get();
	}

	void Dimension::UpdateWorldBorder(const WorldTickContext& context) { (void)context; }
	void Dimension::AdvanceWeatherCycle(const WorldTickContext& context) { (void)context; }
	void Dimension::AdvanceDaylightCycle(const WorldTickContext& context) { (void)context; }
	void Dimension::RunPlayerSleepingLogic(const WorldTickContext& context) { (void)context; }
	void Dimension::RunScheduledCommands(const WorldTickContext& context) { (void)context; }
	void Dimension::RunScheduledBlockTicks(const WorldTickContext& context) { (void)context; }
	void Dimension::RunScheduledFluidTicks(const WorldTickContext& context) { (void)context; }
	void Dimension::RunRaidLogic(const WorldTickContext& context) { (void)context; }
	void Dimension::UpdateChunkLoadLevels(const WorldTickContext& context) { (void)context; }
	void Dimension::TickChunksInRandomOrder(const WorldTickContext& context) { (void)context; }
	void Dimension::SendBlockChanges(const WorldTickContext& context) { (void)context; }
	void Dimension::UpdatePointsOfInterest(const WorldTickContext& context) { (void)context; }
	void Dimension::UnloadChunks(const WorldTickContext& context) { (void)context; }
	void Dimension::TickDragonFight(const WorldTickContext& context) { (void)context; }
	void Dimension::TickEntities(const WorldTickContext& context) { (void)context; }
	void Dimension::TickBlockEntities(const WorldTickContext& context) { (void)context; }
	void Dimension::HandleGameEvents(const WorldTickContext& context) { (void)context; }
}
