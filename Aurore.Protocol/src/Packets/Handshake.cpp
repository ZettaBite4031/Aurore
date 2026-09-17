#include <Aurore/Protocol/Packets/Handshake.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <utility>

namespace Aurore::Protocol::Packets::Handshake::Serverbound {
	ProtocolResult<Packet> Decode(const PacketFrame& frame) {
		if (frame.PacketId != HandshakePacketId)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		Aurore::Util::ByteReader reader(frame.Payload);

		const auto protocol_version = reader.ReadVarInt();
		if (!protocol_version)
			return std::unexpected(ProtocolError::MalformedPacket);

		const auto server_address = reader.ReadString(MaximumServerAddressBytes);
		if (!server_address)
			return std::unexpected(ProtocolError::MalformedPacket);

		const auto server_port = reader.ReadUInt16();
		if (!server_port)
			return std::unexpected(ProtocolError::MalformedPacket);

		const auto intention_value = reader.ReadVarInt();
		if (!intention_value)
			return std::unexpected(ProtocolError::MalformedPacket);

		if (!reader.Empty())
			return std::unexpected(ProtocolError::TrailingPacketData);

		Intention intention;
		switch (*intention_value) {
		case static_cast<std::int32_t>(Intention::Status): intention = Intention::Status; break;
		case static_cast<std::int32_t>(Intention::Login): intention = Intention::Login; break;
		default: return std::unexpected(ProtocolError::InvalidNextState);
		}

		return Packet{ Handshake{
			.ProtocolVersion = *protocol_version,
			.ServerAddress = std::move(*server_address),
			.ServerPort = *server_port,
			.Intention = intention,
		} };
	}
}
