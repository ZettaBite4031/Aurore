#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <bit>
#include <cmath>
#include <utility>

namespace Aurore::Protocol::Packets::Play {
	namespace {
		[[nodiscard]] MovementFlags DecodeMovementFlags(std::uint8_t value) noexcept {
			return MovementFlags{
				.OnGround = (value & 0x01u) != 0,
				.HorizontalCollision = (value & 0x02u) != 0,
			};
		}
		[[nodiscard]] PlayerInputFlags DecodePlayerInputFlags(std::uint8_t value) noexcept {
			return PlayerInputFlags{
				.Forward = (value & 0x01u) != 0,
				.Backward = (value & 0x02u) != 0,
				.Left = (value & 0x04u) != 0,
				.Right = (value & 0x08u) != 0,
				.Jump = (value & 0x10u) != 0,
				.Shift = (value & 0x20u) != 0,
				.Sprint = (value & 0x40u) != 0,
			};
		}
		[[nodiscard]] ProtocolResult<Serverbound::Packet> DecodeEmptyPacket(
			const PacketFrame& frame, Serverbound::Packet packet) {
			if (!frame.Payload.empty())
				return std::unexpected(ProtocolError::TrailingPacketData);
			return packet;
		}
		void EncodeCommonSpawnInfo(Aurore::Util::ByteBuffer& payload, const Clientbound::CommonSpawnInfo& info) {
			payload.WriteVarInt(static_cast<std::int32_t>(info.DimensionType));
			payload.WriteString(info.Dimension.ToString());
			payload.WriteInt64(info.Seed);
			payload.WriteUnsignedByte(static_cast<std::uint8_t>(info.GameModeValue));
			payload.WriteUnsignedByte(std::bit_cast<std::uint8_t>(info.PreviousGameMode));
			payload.WriteBool(info.IsDebug);
			payload.WriteBool(info.IsFlat);
			// The boolean is the optional GlobalPos discriminator.
			payload.WriteBool(info.HasLastDeathLocation);
			payload.WriteVarInt(info.PortalCooldown);
			payload.WriteVarInt(info.SeaLevel);
		}
		[[nodiscard]] PacketFrame Frame(std::int32_t packet_id, const Aurore::Util::ByteBuffer& bytes) {
			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(bytes.Bytes().begin(), bytes.Bytes().end()),
			};
		}
	}

	namespace Clientbound {
		PacketFrame Encode(const BlockChangedAck& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(packet.Sequence);
			return Frame(BlockChangedAckPacketId, payload);
		}

		PacketFrame Encode(const KeepAlive& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteInt64(packet.Id);
			return Frame(KeepAlivePacketId, payload);
		}

		PacketFrame Encode(const ChunkBatchStart&) {
			return PacketFrame{ .PacketId = ChunkBatchStartPacketId };
		}
		PacketFrame Encode(const ChunkBatchFinished& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(packet.BatchSize);
			return Frame(ChunkBatchFinishedPacketId, payload);
		}
		PacketFrame Encode(const GameEvent& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteUnsignedByte(static_cast<std::uint8_t>(packet.Event));
			payload.WriteFloat(packet.Value);
			return Frame(GameEventPacketId, payload);
		}
		PacketFrame Encode(const Login& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteInt32(packet.PlayerId);
			payload.WriteBool(packet.Hardcore);
			payload.WriteVarInt(static_cast<std::int32_t>(packet.Levels.size()));
			for (const auto& level : packet.Levels)
				payload.WriteString(level.ToString());
			payload.WriteVarInt(packet.MaximumPlayers);
			payload.WriteVarInt(packet.ChunkRadius);
			payload.WriteVarInt(packet.SimulationDistance);
			payload.WriteBool(packet.ReducedDebugInfo);
			payload.WriteBool(packet.ShowDeathScreen);
			payload.WriteBool(packet.LimitedCrafting);
			EncodeCommonSpawnInfo(payload, packet.SpawnInfo);
			payload.WriteBool(packet.EnforcesSecureChat);
			return Frame(LoginPacketId, payload);
		}
		PacketFrame Encode(const SetChunkCacheCenter& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(packet.ChunkX);
			payload.WriteVarInt(packet.ChunkZ);
			return Frame(SetChunkCacheCenterPacketId, payload);
		}
		PacketFrame Encode(const SetChunkCacheRadius& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(packet.Radius);
			return Frame(SetChunkCacheRadiusPacketId, payload);
		}
		PacketFrame Encode(const PlayerPosition& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(packet.TeleportId);
			payload.WriteDouble(packet.X);
			payload.WriteDouble(packet.Y);
			payload.WriteDouble(packet.Z);
			payload.WriteDouble(packet.VelocityX);
			payload.WriteDouble(packet.VelocityY);
			payload.WriteDouble(packet.VelocityZ);
			payload.WriteFloat(packet.Yaw);
			payload.WriteFloat(packet.Pitch);
			// Relative.SET_STREAM_CODEC is a fixed-width network INT.
			payload.WriteInt32(static_cast<std::int32_t>(packet.RelativeFlags));
			return Frame(PlayerPositionPacketId, payload);
		}
	}

	namespace Serverbound {
		ProtocolResult<Packet> Decode(const PacketFrame& frame) {
			Aurore::Util::ByteReader reader(frame.Payload);
			switch (frame.PacketId) {
			case AcceptTeleportationPacketId: {
				const auto id = reader.ReadVarInt();
				if (!id) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ AcceptTeleportation{ .TeleportId = *id } };
			}
			case ChunkBatchReceivedPacketId: {
				const auto rate = reader.ReadFloat();
				if (!rate) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ ChunkBatchReceived{ .ChunksPerTick = *rate } };
			}
			case ClientTickEndPacketId: return DecodeEmptyPacket(frame, Packet{ ClientTickEnd{} });
			case ClientInformationPacketId: {
				// Configuration and Play carry the same client preferences payload,
				// but assign different IDs to it. Reuse the validated decoder.
				auto configuration_frame = frame;
				configuration_frame.PacketId = Configuration::Serverbound::ClientInformationPacketId;
				auto decoded = Configuration::Serverbound::Decode(configuration_frame);
				if (!decoded)
					return std::unexpected(decoded.error());

				auto* information = std::get_if<Configuration::Serverbound::ClientInformation>(&*decoded);
				if (information == nullptr)
					return std::unexpected(ProtocolError::MalformedPacket);
				return Packet{ std::move(*information) };
			}
			case KeepAlivePacketId: {
				const auto id = reader.ReadInt64();
				if (!id) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ KeepAlive{ .Id = *id } };
			}
			case MovePlayerPositionPacketId: {
				const auto x = reader.ReadDouble(), y = reader.ReadDouble(), z = reader.ReadDouble();
				const auto flags = reader.ReadUnsignedByte();
				if (!x || !y || !z || !flags) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ MovePlayerPosition{ .X = *x, .Y = *y, .Z = *z, .Flags = DecodeMovementFlags(*flags) } };
			}
			case MovePlayerPositionRotationPacketId: {
				const auto x = reader.ReadDouble(), y = reader.ReadDouble(), z = reader.ReadDouble();
				const auto yaw = reader.ReadFloat(), pitch = reader.ReadFloat();
				const auto flags = reader.ReadUnsignedByte();
				if (!x || !y || !z || !yaw || !pitch || !flags) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ MovePlayerPositionRotation{
					.X = *x, .Y = *y, .Z = *z, .Yaw = *yaw, .Pitch = *pitch,
					.Flags = DecodeMovementFlags(*flags) } };
			}
			case MovePlayerRotationPacketId: {
				const auto yaw = reader.ReadFloat(), pitch = reader.ReadFloat();
				const auto flags = reader.ReadUnsignedByte();
				if (!yaw || !pitch || !flags) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ MovePlayerRotation{ .Yaw = *yaw, .Pitch = *pitch, .Flags = DecodeMovementFlags(*flags) } };
			}
			case MovePlayerStatusOnlyPacketId: {
				const auto flags = reader.ReadUnsignedByte();
				if (!flags) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ MovePlayerStatusOnly{ .Flags = DecodeMovementFlags(*flags) } };
			}
			case PlayerInputPacketId: {
				const auto flags = reader.ReadUnsignedByte();
				if (!flags) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ PlayerInput{ .Flags = DecodePlayerInputFlags(*flags) } };
			}
			case PlayerLoadedPacketId: return DecodeEmptyPacket(frame, Packet{ PlayerLoaded{} });
			case PlayerCommandPacketId: {
				const auto entity = reader.ReadVarInt(), action = reader.ReadVarInt(), boost = reader.ReadVarInt();
				if (!entity || !action || !boost || *entity < 0 || *action < 0 || *action > 6 || *boost < 0 || *boost > 100)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ PlayerCommand{ .EntityId = *entity, .ActionId = *action, .JumpBoost = *boost } };
			}
			case PlayerAbilitiesPacketId: {
				const auto flags = reader.ReadUnsignedByte();
				if (!flags || (*flags & 0xFCu) != 0) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ PlayerAbilities{ .Flying = (*flags & 0x02u) != 0 } };
			}
			case SetCarriedItemPacketId: {
				const auto slot = reader.ReadInt16();
				if (!slot || *slot < 0 || *slot > 8) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ SetCarriedItem{ .Slot = *slot } };
			}
			case ContainerClosePacketId: {
				const auto container = reader.ReadVarInt();
				if (!container || *container < 0) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ ContainerClose{ .ContainerId = *container } };
			}
			case SwingPacketId: {
				const auto hand = reader.ReadVarInt();
				if (!hand || *hand < 0 || *hand > 1) return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ Swing{ .Hand = *hand } };
			}
			case PlayerActionPacketId: {
				const auto action = reader.ReadVarInt();
				const auto position = reader.ReadInt64();
				const auto face = reader.ReadUnsignedByte();
				const auto sequence = reader.ReadVarInt();
				if (!action || !position || !face || !sequence || *action < 0 || *action > 7 || *face > 5 || *sequence < 0)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ DeferredInteraction{ .PacketId = frame.PacketId, .Sequence = *sequence } };
			}
			case UseItemOnPacketId: {
				const auto hand = reader.ReadVarInt();
				const auto position = reader.ReadInt64();
				const auto face = reader.ReadVarInt();
				const auto x = reader.ReadFloat(), y = reader.ReadFloat(), z = reader.ReadFloat();
				const auto inside = reader.ReadBool(), border = reader.ReadBool();
				const auto sequence = reader.ReadVarInt();
				if (!hand || !position || !face || !x || !y || !z || !inside || !border || !sequence
					|| *hand < 0 || *hand > 1 || *face < 0 || *face > 5 || *sequence < 0
					|| !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z))
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ DeferredInteraction{ .PacketId = frame.PacketId, .Sequence = *sequence } };
			}
			case UseItemPacketId: {
				const auto hand = reader.ReadVarInt(), sequence = reader.ReadVarInt();
				const auto yaw = reader.ReadFloat(), pitch = reader.ReadFloat();
				if (!hand || !sequence || !yaw || !pitch || *hand < 0 || *hand > 1 || *sequence < 0
					|| !std::isfinite(*yaw) || !std::isfinite(*pitch))
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty()) return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ DeferredInteraction{ .PacketId = frame.PacketId, .Sequence = *sequence } };
			}
			case SetCreativeModeSlotPacketId: {
				// The modern item-component payload is not decoded in First Light.
				// Hard limit applies *in addition* to the normal framed-packet limit.
				if (frame.Payload.size() > 64 * 1024)
					return std::unexpected(ProtocolError::MalformedPacket);
				const auto slot = reader.ReadInt16();
				if (!slot || reader.Empty()) return std::unexpected(ProtocolError::MalformedPacket);
				return Packet{ DeferredInteraction{ .PacketId = frame.PacketId, .Sequence = std::nullopt } };
			}
			case ContainerClickPacketId: {
				if (frame.Payload.size() > 64 * 1024)
					return std::unexpected(ProtocolError::MalformedPacket);
				const auto container = reader.ReadVarInt(), state = reader.ReadVarInt();
				const auto slot = reader.ReadInt16();
				const auto button = reader.ReadUnsignedByte();
				const auto mode = reader.ReadVarInt();
				if (!container || !state || !slot || !button || !mode || *container < 0 || *state < 0
					|| *mode < 0 || *mode > 6 || reader.Empty())
					return std::unexpected(ProtocolError::MalformedPacket);
				// Changed slot and hashed-slot data intentionally remain opaque.
				return Packet{ DeferredInteraction{ .PacketId = frame.PacketId, .Sequence = std::nullopt } };
			}
			default: return std::unexpected(ProtocolError::UnexpectedPacket);
			}
		}
	}
}
