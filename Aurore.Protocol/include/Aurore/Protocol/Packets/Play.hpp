#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>
#include <Aurore/Util/Registry.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace Aurore::Protocol::Packets::Play {
	inline constexpr std::int32_t TargetProtocolVersion{ 774 };

	enum class Direction : std::uint8_t { Clientbound, Serverbound };

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

	struct PlayerInputFlags final {
		bool Forward{ false };
		bool Backward{ false };
		bool Left{ false };
		bool Right{ false };
		bool Jump{ false };
		bool Shift{ false };
		bool Sprint{ false };
		auto operator<=>(const PlayerInputFlags&) const noexcept = default;
	};

	enum class GameMode : std::int8_t { Survival = 0, Creative = 1, Adventure = 2, Spectator = 3 };

	namespace Clientbound {
		inline constexpr std::int32_t BlockChangedAckPacketId{ 0x04 };
		inline constexpr std::int32_t KeepAlivePacketId{ 0x2B };
		inline constexpr std::int32_t ChunkBatchFinishedPacketId{ 0x0B };
		inline constexpr std::int32_t ChunkBatchStartPacketId{ 0x0C };
		inline constexpr std::int32_t GameEventPacketId{ 0x26 };
		inline constexpr std::int32_t LoginPacketId{ 0x30 };
		inline constexpr std::int32_t PlayerPositionPacketId{ 0x46 };
		inline constexpr std::int32_t SetChunkCacheCenterPacketId{ 0x5C };
		inline constexpr std::int32_t SetChunkCacheRadiusPacketId{ 0x5D };

		inline constexpr std::array PacketDescriptors{
			PacketDescriptor{ BlockChangedAckPacketId, "block_changed_ack", Direction::Clientbound },
			PacketDescriptor{ KeepAlivePacketId, "keep_alive", Direction::Clientbound },
			PacketDescriptor{ ChunkBatchFinishedPacketId, "chunk_batch_finished", Direction::Clientbound },
			PacketDescriptor{ ChunkBatchStartPacketId, "chunk_batch_start", Direction::Clientbound },
			PacketDescriptor{ GameEventPacketId, "game_event", Direction::Clientbound },
			PacketDescriptor{ LoginPacketId, "login", Direction::Clientbound },
			PacketDescriptor{ PlayerPositionPacketId, "player_position", Direction::Clientbound },
			PacketDescriptor{ SetChunkCacheCenterPacketId, "set_chunk_cache_center", Direction::Clientbound },
			PacketDescriptor{ SetChunkCacheRadiusPacketId, "set_chunk_cache_radius", Direction::Clientbound },
		};

		enum class GameEventType : std::uint8_t { StartWaitingForLevelChunks = 13 };

		struct BlockChangedAck final {
			std::int32_t Sequence{ 0 };
			bool operator==(const BlockChangedAck&) const noexcept = default;
		};

		struct KeepAlive final {
			std::int64_t Id{ 0 };
			bool operator==(const KeepAlive&) const noexcept = default;
		};

		struct ChunkBatchStart final {
			bool operator==(const ChunkBatchStart&) const noexcept = default;
		};
		struct ChunkBatchFinished final {
			std::int32_t BatchSize{ 0 };
			bool operator==(const ChunkBatchFinished&) const noexcept = default;
		};
		struct GameEvent final {
			GameEventType Event{ GameEventType::StartWaitingForLevelChunks };
			float Value{ 0.0F };
			bool operator==(const GameEvent&) const noexcept = default;
		};
		struct SetChunkCacheCenter final {
			std::int32_t ChunkX{ 0 };
			std::int32_t ChunkZ{ 0 };
			bool operator==(const SetChunkCacheCenter&) const noexcept = default;
		};
		struct SetChunkCacheRadius final {
			std::int32_t Radius{ 2 };
			bool operator==(const SetChunkCacheRadius&) const noexcept = default;
		};
		struct CommonSpawnInfo final {
			Aurore::Util::RegistryRuntimeId DimensionType{ 0 };
			Aurore::Util::ResourceLocation Dimension;
			std::int64_t Seed{ 0 };
			GameMode GameModeValue{ GameMode::Creative };
			std::int8_t PreviousGameMode{ -1 };
			bool IsDebug{ false };
			bool IsFlat{ false };
			bool HasLastDeathLocation{ false };
			std::int32_t PortalCooldown{ 0 };
			std::int32_t SeaLevel{ 63 };
			bool operator==(const CommonSpawnInfo&) const noexcept = default;
		};
		struct Login final {
			std::int32_t PlayerId{ 0 };
			bool Hardcore{ false };
			std::vector<Aurore::Util::ResourceLocation> Levels;
			std::int32_t MaximumPlayers{ 20 };
			std::int32_t ChunkRadius{ 2 };
			std::int32_t SimulationDistance{ 2 };
			bool ReducedDebugInfo{ false };
			bool ShowDeathScreen{ true };
			bool LimitedCrafting{ false };
			CommonSpawnInfo SpawnInfo;
			bool EnforcesSecureChat{ false };
			bool operator==(const Login&) const noexcept = default;
		};
		struct PlayerPosition final {
			std::int32_t TeleportId{ 1 };
			double X{ 0.0 };
			double Y{ 64.0 };
			double Z{ 0.0 };
			double VelocityX{ 0.0 };
			double VelocityY{ 0.0 };
			double VelocityZ{ 0.0 };
			float Yaw{ 0.0F };
			float Pitch{ 0.0F };
			std::uint32_t RelativeFlags{ 0 };
			bool operator==(const PlayerPosition&) const noexcept = default;
		};

		[[nodiscard]] PacketFrame Encode(const BlockChangedAck& packet);
		[[nodiscard]] PacketFrame Encode(const KeepAlive& packet);
		[[nodiscard]] PacketFrame Encode(const ChunkBatchStart& packet);
		[[nodiscard]] PacketFrame Encode(const ChunkBatchFinished& packet);
		[[nodiscard]] PacketFrame Encode(const GameEvent& packet);
		[[nodiscard]] PacketFrame Encode(const Login& packet);
		[[nodiscard]] PacketFrame Encode(const PlayerPosition& packet);
		[[nodiscard]] PacketFrame Encode(const SetChunkCacheCenter& packet);
		[[nodiscard]] PacketFrame Encode(const SetChunkCacheRadius& packet);
	}

	namespace Serverbound {
		inline constexpr std::int32_t AcceptTeleportationPacketId{ 0x00 };
		inline constexpr std::int32_t ChunkBatchReceivedPacketId{ 0x0A };
		inline constexpr std::int32_t ClientTickEndPacketId{ 0x0C };
		inline constexpr std::int32_t ClientInformationPacketId{ 0x0D };
		inline constexpr std::int32_t ContainerClickPacketId{ 0x11 };
		inline constexpr std::int32_t ContainerClosePacketId{ 0x12 };
		inline constexpr std::int32_t KeepAlivePacketId{ 0x1B };
		inline constexpr std::int32_t MovePlayerPositionPacketId{ 0x1D };
		inline constexpr std::int32_t MovePlayerPositionRotationPacketId{ 0x1E };
		inline constexpr std::int32_t MovePlayerRotationPacketId{ 0x1F };
		inline constexpr std::int32_t MovePlayerStatusOnlyPacketId{ 0x20 };
		inline constexpr std::int32_t PlayerAbilitiesPacketId{ 0x27 };
		inline constexpr std::int32_t PlayerActionPacketId{ 0x28 };
		inline constexpr std::int32_t PlayerCommandPacketId{ 0x29 };
		inline constexpr std::int32_t PlayerInputPacketId{ 0x2A };
		inline constexpr std::int32_t PlayerLoadedPacketId{ 0x2B };
		inline constexpr std::int32_t SetCarriedItemPacketId{ 0x34 };
		inline constexpr std::int32_t SetCreativeModeSlotPacketId{ 0x37 };
		inline constexpr std::int32_t SwingPacketId{ 0x3C };
		inline constexpr std::int32_t UseItemOnPacketId{ 0x3F };
		inline constexpr std::int32_t UseItemPacketId{ 0x40 };

		inline constexpr std::array PacketDescriptors{
			PacketDescriptor{ AcceptTeleportationPacketId, "accept_teleportation", Direction::Serverbound },
			PacketDescriptor{ ChunkBatchReceivedPacketId, "chunk_batch_received", Direction::Serverbound },
			PacketDescriptor{ ClientTickEndPacketId, "client_tick_end", Direction::Serverbound },
			PacketDescriptor{ ClientInformationPacketId, "client_information", Direction::Serverbound },
			PacketDescriptor{ ContainerClickPacketId, "container_click", Direction::Serverbound },
			PacketDescriptor{ ContainerClosePacketId, "container_close", Direction::Serverbound },
			PacketDescriptor{ KeepAlivePacketId, "keep_alive", Direction::Serverbound },
			PacketDescriptor{ MovePlayerPositionPacketId, "move_player_pos", Direction::Serverbound },
			PacketDescriptor{ MovePlayerPositionRotationPacketId, "move_player_pos_rot", Direction::Serverbound },
			PacketDescriptor{ MovePlayerRotationPacketId, "move_player_rot", Direction::Serverbound },
			PacketDescriptor{ MovePlayerStatusOnlyPacketId, "move_player_status_only", Direction::Serverbound },
			PacketDescriptor{ PlayerAbilitiesPacketId, "player_abilities", Direction::Serverbound },
			PacketDescriptor{ PlayerActionPacketId, "player_action", Direction::Serverbound },
			PacketDescriptor{ PlayerCommandPacketId, "player_command", Direction::Serverbound },
			PacketDescriptor{ PlayerInputPacketId, "player_input", Direction::Serverbound },
			PacketDescriptor{ PlayerLoadedPacketId, "player_loaded", Direction::Serverbound },
			PacketDescriptor{ SetCarriedItemPacketId, "set_carried_item", Direction::Serverbound },
			PacketDescriptor{ SetCreativeModeSlotPacketId, "set_creative_mode_slot", Direction::Serverbound },
			PacketDescriptor{ SwingPacketId, "swing", Direction::Serverbound },
			PacketDescriptor{ UseItemOnPacketId, "use_item_on", Direction::Serverbound },
			PacketDescriptor{ UseItemPacketId, "use_item", Direction::Serverbound },
		};

		struct AcceptTeleportation final { std::int32_t TeleportId{ 0 }; auto operator<=>(const AcceptTeleportation&) const noexcept = default; };
		struct ChunkBatchReceived final { float ChunksPerTick{ 0.0F }; bool operator==(const ChunkBatchReceived&) const noexcept = default; };
		struct ClientTickEnd final { auto operator<=>(const ClientTickEnd&) const noexcept = default; };
		struct KeepAlive final { std::int64_t Id{ 0 }; auto operator<=>(const KeepAlive&) const noexcept = default; };
		struct MovePlayerPosition final { double X{ 0.0 }; double Y{ 0.0 }; double Z{ 0.0 }; MovementFlags Flags; bool operator==(const MovePlayerPosition&) const noexcept = default; };
		struct MovePlayerPositionRotation final { double X{ 0.0 }; double Y{ 0.0 }; double Z{ 0.0 }; float Yaw{ 0.0F }; float Pitch{ 0.0F }; MovementFlags Flags; bool operator==(const MovePlayerPositionRotation&) const noexcept = default; };
		struct MovePlayerRotation final { float Yaw{ 0.0F }; float Pitch{ 0.0F }; MovementFlags Flags; bool operator==(const MovePlayerRotation&) const noexcept = default; };
		struct MovePlayerStatusOnly final { MovementFlags Flags; auto operator<=>(const MovePlayerStatusOnly&) const noexcept = default; };
		struct PlayerInput final { PlayerInputFlags Flags; auto operator<=>(const PlayerInput&) const noexcept = default; };
		// Both protocol states use the same validated client preference payload.
		using ClientInformation = Packets::Configuration::Serverbound::ClientInformation;

		struct PlayerLoaded final { auto operator<=>(const PlayerLoaded&) const noexcept = default; };

		struct PlayerCommand final {
			std::int32_t EntityId{ 0 };
			std::int32_t ActionId{ 0 };
			std::int32_t JumpBoost{ 0 };
		};
		struct PlayerAbilities final { bool Flying{ false }; };
		struct SetCarriedItem final { std::int16_t Slot{ 0 }; };
		struct ContainerClose final { std::int32_t ContainerId{ 0 }; };
		struct Swing final { std::int32_t Hand{ 0 }; };

		// No item or world state is changed by these deliberately deferred actions.
		// Optional sequence numbers are acknowledged to complete client prediction.
		struct DeferredInteraction final {
			std::int32_t PacketId{ 0 };
			std::optional<std::int32_t> Sequence;
		};

		using Packet = std::variant<
			AcceptTeleportation, ChunkBatchReceived, ClientTickEnd, KeepAlive,
			MovePlayerPosition, MovePlayerPositionRotation, MovePlayerRotation,
			MovePlayerStatusOnly, PlayerInput, PlayerLoaded, ClientInformation, PlayerCommand,
			PlayerAbilities, SetCarriedItem, ContainerClose, Swing, DeferredInteraction>;

		[[nodiscard]] ProtocolResult<Packet> Decode(const PacketFrame& frame);
	}
}
