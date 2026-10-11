#include <Aurore/World/WorldManager.hpp>

#include <limits>
#include <memory>
#include <utility>

namespace {
	constexpr std::string_view OverworldIdentifier{ "minecraft:overworld" };
	constexpr std::string_view NetherIdentifier{ "minecraft:the_nether" };
	constexpr std::string_view EndIdentifier{ "minecraft:the_end" };
}

namespace Aurore::World {
	WorldManager::~WorldManager() noexcept {
		Shutdown();
	}

	bool WorldManager::Initialize() {
		if (m_Initialized) return true;

		if (!m_Overworld)
			m_Overworld = std::make_unique<Dimension>(DimensionKind::Overworld, std::string(OverworldIdentifier));
		if (!m_Nether)
			m_Nether = std::make_unique<Dimension>(DimensionKind::Nether, std::string(NetherIdentifier));
		if (!m_End)
			m_End = std::make_unique<Dimension>(DimensionKind::End, std::string(EndIdentifier));

		if (!InitializeDimension(*m_Overworld)) {
			Shutdown();
			return false;
		}

		if (!InitializeDimension(*m_Nether)) {
			Shutdown();
			return false;
		}

		if (!InitializeDimension(*m_End)) {
			Shutdown();
			return false;
		}

		for (auto& dimension : m_CustomDimensions) {
			if (!InitializeDimension(*dimension)) {
				Shutdown();
				return false;
			}
		}

		m_Initialized = true;
		return true;
	}

	void WorldManager::RunTick(const WorldTickContext& context) {
		if (!m_Initialized) return;

		/*
			The vanilla dimension order is explicit and intentional:

				1. Overworld
				2. Nether
				3. End
				4. Custom dimensions

			Do not replace the built-in dimensions with iteration over a
			hash table or registry whose traversal order is unspecified.
		*/

		m_Overworld->RunTick(context);
		m_Nether->RunTick(context);
		m_End->RunTick(context);

		for (auto& dimension : m_CustomDimensions)
			dimension->RunTick(context);
	}

	void WorldManager::Shutdown() noexcept {
		/*
			Shutdown in reverse tick/initialization order.

			The Dimension objects are retained, allowing the manager to be
			reinitialized without reconstructing registrations.
		*/

		for (auto iterator = m_CustomDimensions.rbegin(); iterator != m_CustomDimensions.rend(); iterator++) {
			(*iterator)->Shutdown();
		}

		if (m_End) m_End->Shutdown();
		if (m_Nether) m_Nether->Shutdown();
		if (m_Overworld) m_Overworld->Shutdown();

		m_Initialized = false;
	}

	bool WorldManager::RegisterCustomDimension(std::unique_ptr<Dimension> dimension) {
		if (!dimension) return false;

		if (dimension->GetKind() != DimensionKind::Custom) return false;
		if (dimension->GetIdentifier().empty()) return false;
		if (FindDimension(dimension->GetIdentifier()) != nullptr) return false;
		if (m_Initialized && !dimension->Initialize()) return false;

		m_CustomDimensions.push_back(std::move(dimension));

		return true;
	}

	Player* WorldManager::CreatePlayer(Aurore::Util::Uuid unique_id, std::string username, Dimension& dimension, PlayerPosition spawn_position) {
		if (!m_Initialized || unique_id.IsNil() || username.empty() || !dimension.IsInitialized())
			return nullptr;

		const auto entity_id = AllocatePlayerEntityId();
		if (entity_id <= 0) return nullptr;

		auto player = std::make_unique<Player>(entity_id, std::move(unique_id), std::move(username), dimension, spawn_position);
		auto result = player.get();
		const auto [iterator, inserted] = m_Players.emplace(entity_id, std::move(player));
		if (!inserted) return nullptr;
		return result;
	}

	bool WorldManager::RemovePlayer(PlayerEntityId entity_id) noexcept {
		return m_Players.erase(entity_id) != 0;
	}

	Player* WorldManager::FindPlayer(PlayerEntityId entity_id) noexcept {
		const auto iterator = m_Players.find(entity_id);
		return iterator == m_Players.end() ? nullptr : iterator->second.get();
	}

	const Player* WorldManager::FindPlayer(PlayerEntityId entity_id) const noexcept {
		const auto iterator = m_Players.find(entity_id);
		return iterator == m_Players.end() ? nullptr : iterator->second.get();
	}

	std::size_t WorldManager::GetPlayerCount() const noexcept {
		return m_Players.size();
	}

	Dimension* WorldManager::FindDimension(std::string_view identifier) noexcept {
		if (m_Overworld && m_Overworld->GetIdentifier() == identifier) return m_Overworld.get();
		if (m_Nether && m_Nether->GetIdentifier() == identifier) return m_Nether.get();
		if (m_End && m_End->GetIdentifier() == identifier) return m_End.get();

		for (auto& dimension : m_CustomDimensions)
			if (dimension->GetIdentifier() == identifier)
				return dimension.get();

		return nullptr;
	}

	const Dimension* WorldManager::FindDimension(std::string_view identifier) const noexcept {
		if (m_Overworld && m_Overworld->GetIdentifier() == identifier) return m_Overworld.get();
		if (m_Nether && m_Nether->GetIdentifier() == identifier) return m_Nether.get();
		if (m_End && m_End->GetIdentifier() == identifier) return m_End.get();

		for (auto& dimension : m_CustomDimensions)
			if (dimension->GetIdentifier() == identifier)
				return dimension.get();

		return nullptr;
	}

	Dimension* WorldManager::GetOverworld() noexcept { return m_Overworld.get(); }
	const Dimension* WorldManager::GetOverworld() const noexcept { return m_Overworld.get(); }

	const Dimension* WorldManager::GetNether() const noexcept { return m_Nether.get(); }
	Dimension* WorldManager::GetNether() noexcept { return m_Nether.get(); }

	const Dimension* WorldManager::GetEnd() const noexcept { return m_End.get(); }
	Dimension* WorldManager::GetEnd() noexcept { return m_End.get(); }

	bool WorldManager::IsInitialized() const noexcept { return m_Initialized; }

	bool WorldManager::InitializeDimension(Dimension& dimension) { return dimension.Initialize(); }

	PlayerEntityId WorldManager::AllocatePlayerEntityId() noexcept {
		if (m_NextPlayerEntityId <= 0) return 0;

		const auto entity_id = m_NextPlayerEntityId;
		if (entity_id == std::numeric_limits<PlayerEntityId>::max())
			m_NextPlayerEntityId = 0;
		else m_NextPlayerEntityId++;

		return entity_id;
	}
}
