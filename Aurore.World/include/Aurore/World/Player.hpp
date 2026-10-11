#pragma once

#include <Aurore/Util/UUID.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Aurore::World {
	class Dimension;

	using PlayerEntityId = std::int32_t;

	struct PlayerPosition final {
		double X{ 0.5 };
		double Y{ 64.0 };
		double Z{ 0.5 };
		float Yaw{ 0.f };
		float Pitch{ 0.f };
		bool OnGround{ false };
		bool HorizontalCollision{ false };

		bool operator==(const PlayerPosition&) const noexcept = default;
	};

	struct PlayerMovementUpdate final {
		std::optional<double> X;
		std::optional<double> Y;
		std::optional<double> Z;
		std::optional<float> Yaw;
		std::optional<float> Pitch;
		bool OnGround{ false };
		bool HorizontalCollision{ false };
	};

	class Player final {
	public:
		Player(PlayerEntityId entity_id, Aurore::Util::Uuid unique_id, std::string username, Dimension& dimension, PlayerPosition spawn_position);

		Player(const Player&) = delete;
		Player& operator=(const Player&) = delete;
		Player(Player&&) = delete;
		Player& operator=(Player&&) = delete;

		[[nodiscard]] bool ApplyMovement(const PlayerMovementUpdate& update) noexcept;
		void MarkLoaded() noexcept;

		[[nodiscard]] PlayerEntityId GetEntityId() const noexcept;
		[[nodiscard]] const Aurore::Util::Uuid& GetUniqueId() const noexcept;
		[[nodiscard]] std::string_view GetUsername() const noexcept;
		[[nodiscard]] Dimension& GetDimension() noexcept;
		[[nodiscard]] const Dimension& GetDimension() const noexcept;
		[[nodiscard]] const PlayerPosition& GetPosition() const noexcept;
		[[nodiscard]] bool IsLoaded() const noexcept;

	private:
		PlayerEntityId m_EntityId{ 0 };
		Aurore::Util::Uuid m_UniqueId;
		std::string m_Username;
		Dimension* m_Dimension{ nullptr };
		PlayerPosition m_Position;
		bool m_Loaded{ false };
	};
}
