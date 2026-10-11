#include <Aurore/Protocol/Packets/Play.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

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
