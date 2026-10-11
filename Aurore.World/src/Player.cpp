#include <Aurore/World/Player.hpp>

#include <Aurore/World/Dimension.hpp>

#include <cmath>
#include <utility>

namespace Aurore::World {
	Player::Player(PlayerEntityId entity_id, Aurore::Util::Uuid unique_id, std::string username, Dimension& dimension, PlayerPosition spawn_position)
		: m_EntityId(entity_id), m_UniqueId(std::move(unique_id)), m_Username(std::move(username)), m_Dimension(&dimension), m_Position(spawn_position) {}

	bool Player::ApplyMovement(const PlayerMovementUpdate& update) noexcept {
		const auto finite_double = [](const std::optional<double>& value) noexcept { return !value || std::isfinite(*value); };
		const auto finite_float = [](const std::optional<float>& value) noexcept { return !value || std::isfinite(*value); };

		if (!finite_double(update.X) ||
			!finite_double(update.Y) ||
			!finite_double(update.Z) ||
			!finite_float(update.Yaw) ||
			!finite_float(update.Pitch))
			return false;

		if (update.X) m_Position.X = *update.X;
		if (update.Y) m_Position.Y = *update.Y;
		if (update.Z) m_Position.Z = *update.Z;
		if (update.Yaw) m_Position.Yaw = *update.Yaw;
		if (update.Pitch) m_Position.Pitch = *update.Pitch;

		m_Position.OnGround = update.OnGround;
		m_Position.HorizontalCollision = update.HorizontalCollision;
		return true;
	}

	void Player::MarkLoaded() noexcept { m_Loaded = true; }

	PlayerEntityId Player::GetEntityId() const noexcept { return m_EntityId; }
	const Aurore::Util::Uuid& Player::GetUniqueId() const noexcept { return m_UniqueId; }
	std::string_view Player::GetUsername() const noexcept { return m_Username; }
	Dimension& Player::GetDimension() noexcept { return *m_Dimension; }
	const Dimension& Player::GetDimension() const noexcept { return *m_Dimension; }
	const PlayerPosition& Player::GetPosition() const noexcept { return m_Position; }
	bool Player::IsLoaded() const noexcept { return m_Loaded; }
}
