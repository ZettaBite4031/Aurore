#include <Aurore/Protocol/Packets/Login.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Aurore::Protocol::Packets::Login {
	namespace {
		PacketFrame MakeFrame(std::int32_t packet_id, const Aurore::Util::ByteBuffer& payload) {
			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end())
			};
		}
	}

	namespace Serverbound {
		ProtocolResult<Packet> Decode(const PacketFrame& frame) {
			switch (frame.PacketId) {
			case StartPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				auto username = reader.ReadString(MaximumUsernameEncodedBytes);
				if (!username)
					return std::unexpected(ProtocolError::MalformedPacket);
				const auto presented_profile_id = reader.ReadUuid();
				if (!presented_profile_id)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ Start{
					.Username = std::move(*username),
					.PresentedProfileId = *presented_profile_id,
				} };
			} break;
			case AcknowledgedPacketId: {
				if (!frame.Payload.empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ Acknowledged{} };
			} break;
			default:
				return std::unexpected(ProtocolError::UnexpectedPacket);
			}
		}
	}

	namespace Clientbound {
		PacketFrame Encode(const Disconnect& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteString(packet.ReasonJson);
			return MakeFrame(DisconnectPacketId, payload);
		}

		PacketFrame Encode(const Success& packet) {
			if (packet.Properties.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
				throw std::length_error("Too many properties!");

			Aurore::Util::ByteBuffer payload;

			payload.WriteUuid(packet.ProfileId);
			payload.WriteString(packet.Username);
			payload.WriteVarInt(static_cast<std::int32_t>(packet.Properties.size()));
			for (const auto& property : packet.Properties) {
				payload.WriteString(property.Name);
				payload.WriteString(property.Value);
				payload.WriteBool(property.Signature.has_value());
				if (property.Signature)
					payload.WriteString(*property.Signature);
			}

			return MakeFrame(SuccessPacketId, payload);
		}

		PacketFrame Encode(const Packet& packet) {
			return std::visit([](const auto& value) { return Encode(value); }, packet);
		}
	}
}
