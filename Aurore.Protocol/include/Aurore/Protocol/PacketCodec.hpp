#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Status.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>

#include <variant>

namespace Aurore::Protocol {
	/*
		ConfigurationSequenceBuilder has already validated and encoded these
		frames. They still pass through ProtocolPipeline so compression,
		framing, and encryption remain connection-owned.
	*/
	struct PreparedPacketFrame final {
		PacketFrame Frame;
	};

	using ClientboundPacket = std::variant<
		Packets::Status::Clientbound::Response,
		Packets::Status::Clientbound::Pong,
		Packets::Login::Clientbound::Disconnect,
		Packets::Login::Clientbound::Success,
		PreparedPacketFrame>;

	/*
		The variant is consumed by value. Passing an rvalue transfers ownership
		of PreparedPacketFrame payload storage instead of copying it.
	*/
	[[nodiscard]] PacketFrame EncodeClientboundPacket(ClientboundPacket packet);
}

