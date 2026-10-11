#include <Aurore/Protocol/Packets/Play.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <bit>

namespace Aurore::Protocol::Packets::Play {
	namespace {
		[[nodiscard]] MovementFlags DecodeMovementFlags(std::uint8_t value) noexcept {
			return MovementFlags{ .OnGround = (value & 0x01u) != 0, .HorizontalCollision = (value & 0x02u) != 0 };
		}

		[[nodiscard]] ProtocolResult<Serverbound::Packet> DecodeEmptyPacket(const PacketFrame& frame, Serverbound::Packet packet) {
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

			/*
				GlobalPos is deliberately absent for First Light. The boolean is
				the Optional<GlobalPos> discriminator used by vanilla.
			*/
			payload.WriteBool(info.HasLastDeathLocation);
			payload.WriteVarInt(info.PortalCooldown);
			payload.WriteVarInt(info.SeaLevel);
		}
	}

	namespace Clientbound {
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

			return PacketFrame{
				.PacketId = LoginPacketId,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end()),
			};
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

			/* Relative.SET_STREAM_CODEC is a fixed-width network INT. */
			payload.WriteInt32(static_cast<std::int32_t>(packet.RelativeFlags));

			return PacketFrame{
				.PacketId = PlayerPositionPacketId,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end()),
			};
		}
	}

	namespace Serverbound {
		ProtocolResult<Packet> Decode(const PacketFrame& frame) {
			switch (frame.PacketId) {
			case AcceptTeleportationPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto teleport_id = reader.ReadVarInt();

				if (!teleport_id)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ AcceptTeleportation{.TeleportId = *teleport_id } };
			}
			case ClientTickEndPacketId: return DecodeEmptyPacket(frame, Packet{ ClientTickEnd{} });
			case KeepAlivePacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto id = reader.ReadInt64();

				if (!id)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ KeepAlive{.Id = *id} };
			}
			case MovePlayerPositionPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto x = reader.ReadDouble();
				const auto y = reader.ReadDouble();
				const auto z = reader.ReadDouble();
				const auto flags = reader.ReadUnsignedByte();

				if (!x || !y || !z || !flags)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);

				return Packet{
					MovePlayerPosition{
						.X = *x,
						.Y = *y,
						.Z = *z,
						.Flags = DecodeMovementFlags(*flags),
					}
				};
			}
			case MovePlayerPositionRotationPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto x = reader.ReadDouble();
				const auto y = reader.ReadDouble();
				const auto z = reader.ReadDouble();
				const auto yaw = reader.ReadFloat();
				const auto pitch = reader.ReadFloat();
				const auto flags = reader.ReadUnsignedByte();

				if (!x || !y || !z || !yaw || !pitch || !flags)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);

				return Packet{
					MovePlayerPositionRotation{
						.X = *x,
						.Y = *y,
						.Z = *z,
						.Yaw = *yaw,
						.Pitch = *pitch,
						.Flags = DecodeMovementFlags(*flags),
					}
				};
			} 
			case MovePlayerRotationPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto yaw = reader.ReadFloat();
				const auto pitch = reader.ReadFloat();
				const auto flags = reader.ReadUnsignedByte();

				if (!yaw || !pitch || !flags)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);

				return Packet{
					MovePlayerRotation{
						.Yaw = *yaw,
						.Pitch = *pitch,
						.Flags = DecodeMovementFlags(*flags),
					}
				};
			}
			case MovePlayerStatusOnlyPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				const auto flags = reader.ReadUnsignedByte();

				if (!flags)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);

				return Packet{
					MovePlayerStatusOnly{
						.Flags = DecodeMovementFlags(*flags),
					}
				};
			}
			case PlayerLoadedPacketId: return DecodeEmptyPacket(frame, Packet{ PlayerLoaded{} });
			default: return std::unexpected(ProtocolError::UnexpectedPacket);
			}
		}
	}
}
