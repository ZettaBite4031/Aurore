#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <string_view>
#include <variant>

namespace Aurore::Protocol::Packets::Play {
	inline constexpr std::int32_t TargetProtocolVersion{ 774 };

	enum class Direction : std::uint8_t {
		Clientbound,
		Serverbound,
	};

	struct PacketDescriptor final {
		std::int32_t Id;
		std::string_view Name;
		Direction PacketDirection;

		auto operator<=>(const PacketDescriptor&) const noexcept = default;
	};

	struct MovementFlags final {
		bool OnGround{ false };
		bool HorizontalCollision{ false };

		auto operator<=>(const MovementFlags&) const noexcept = default;
	};

	namespace Serverbound {
		inline constexpr std::int32_t AcceptTeleportationPacketId{ 0x00 };
		inline constexpr std::int32_t ClientTickEndPacketId{ 0xC };
		inline constexpr std::int32_t KeepAlivePacketId{ 0x1B };
		inline constexpr std::int32_t MovePlayerPositionPacketId{ 0x1D };
		inline constexpr std::int32_t MovePlayerPositionRotationPacketId{ 0x1E };
		inline constexpr std::int32_t MovePlayerRotationPacketId{ 0x1F };
		inline constexpr std::int32_t MovePlayerStatusOnlyPacketId{ 0x20 };
		inline constexpr std::int32_t PlayerLoadedPacketId{ 0x2B };

		inline constexpr std::array PacketDescriptors{
			PacketDescriptor{ AcceptTeleportationPacketId, "accept_teleportation", Direction::Serverbound },
			PacketDescriptor{ ClientTickEndPacketId, "client_tick_end", Direction::Serverbound },
			PacketDescriptor{ KeepAlivePacketId, "keep_alive", Direction::Serverbound },
			PacketDescriptor{ MovePlayerPositionPacketId, "move_player_pos", Direction::Serverbound },
			PacketDescriptor{ MovePlayerPositionRotationPacketId, "move_player_pos_rot", Direction::Serverbound },
			PacketDescriptor{ MovePlayerRotationPacketId, "move_player_rot", Direction::Serverbound },
			PacketDescriptor{ MovePlayerStatusOnlyPacketId, "move_player_status_only", Direction::Serverbound },
			PacketDescriptor{ PlayerLoadedPacketId, "player_loaded", Direction::Serverbound },
		};

		struct AcceptTeleportation final {
			std::int32_t TeleportId{ 0 };

			auto operator<=>(const AcceptTeleportation&) const noexcept = default;
		};

		struct ClientTickEnd final {
			auto operator<=>(const ClientTickEnd&) const noexcept = default;
		};

		struct KeepAlive final {
			std::int64_t Id{ 0 };

			auto operator<=>(const KeepAlive&) const noexcept = default;
		};

		struct MovePlayerPosition final {
			double X{ 0.0 };
			double Y{ 0.0 };
			double Z{ 0.0 };
			MovementFlags Flags;

			bool operator==(const MovePlayerPosition&) const noexcept = default;
		};

		struct MovePlayerPositionRotation final {
			double X{ 0.0 };
			double Y{ 0.0 };
			double Z{ 0.0 };
			float Yaw{ 0.0F };
			float Pitch{ 0.0F };
			MovementFlags Flags;

			bool operator==(const MovePlayerPositionRotation&) const noexcept = default;
		};

		struct MovePlayerRotation final {
			float Yaw{ 0.0F };
			float Pitch{ 0.0F };
			MovementFlags Flags;

			bool operator==(const MovePlayerRotation&) const noexcept = default;
		};

		struct MovePlayerStatusOnly final {
			MovementFlags Flags;

			auto operator<=>(const MovePlayerStatusOnly&) const noexcept = default;
		};

		struct PlayerLoaded final {
			auto operator<=>(const PlayerLoaded&) const noexcept = default;
		};

		using Packet = std::variant<
			AcceptTeleportation,
			ClientTickEnd,
			KeepAlive,
			MovePlayerPosition,
			MovePlayerPositionRotation,
			MovePlayerRotation,
			MovePlayerStatusOnly,
			PlayerLoaded>;

		[[nodiscard]] ProtocolResult<Packet> Decode(const PacketFrame& frame);
	}
}
