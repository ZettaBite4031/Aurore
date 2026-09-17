#pragma once

#include <Aurore/World/Dimension.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace Aurore::World {
	class WorldManager final {
	public:
		WorldManager() = default;
		~WorldManager() noexcept;

		WorldManager(const WorldManager&) = delete;
		WorldManager& operator=(const WorldManager&) = delete;

		WorldManager(WorldManager&&) = delete;
		WorldManager& operator=(WorldManager&&) = delete;

		[[nodiscard]] bool Initialize();

		void RunTick(const WorldTickContext& context);

		void Shutdown() noexcept;

		[[nodiscard]] bool RegisterCustomDimension(std::unique_ptr<Dimension> dimension);

		[[nodiscard]] Dimension* FindDimension(std::string_view identifier) noexcept;
		[[nodiscard]] const Dimension* FindDimension(std::string_view identifier) const noexcept;

		[[nodiscard]] Dimension* GetOverworld() noexcept;
		[[nodiscard]] const Dimension* GetOverworld() const noexcept;

		[[nodiscard]] Dimension* GetNether() noexcept;
		[[nodiscard]] const Dimension* GetNether() const noexcept;

		[[nodiscard]] Dimension* GetEnd() noexcept;
		[[nodiscard]] const Dimension* GetEnd() const noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		[[nodiscard]] bool InitializeDimension(Dimension& dimension);

		std::unique_ptr<Dimension> m_Overworld;
		std::unique_ptr<Dimension> m_Nether;
		std::unique_ptr<Dimension> m_End;

		/*
			Vector order is registration order.

			Custom dimensions must be ticked after the three vanilla
			dimensions and in deterministic registration order.
		*/
		std::vector<std::unique_ptr<Dimension>> m_CustomDimensions;

		bool m_Initialized{ false };
	};
}
