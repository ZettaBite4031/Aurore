#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <cstdint>
#include <variant>

namespace Aurore::Protocol::Packets::Status {
	namespace Serverbound {
		inline constexpr std::int32_t RequestPacketId{ 0x00 };
		inline constexpr std::int32_t PingPacketId{ 0x01 };

		struct Request final {};
		struct Ping final { std::int64_t Payload{ 0 }; };

		using Packet = std::variant<Request, Ping>;

		[[nodiscard]] ProtocolResult<Packet> Decode(const PacketFrame& frame);
	}

	namespace Clientbound {
		inline constexpr std::int32_t ResponsePacketId{ 0x00 };
		inline constexpr std::int32_t PongPacketId{ 0x01 };

		struct Response final { ServerStatus Status; };
		struct Pong final { std::int64_t Payload{ 0 }; };

		using Packet = std::variant<Response, Pong>;

		[[nodiscard]] PacketFrame Encode(const Response& packet);
		[[nodiscard]] PacketFrame Encode(const Pong& packet);
		[[nodiscard]] PacketFrame Encode(const Packet& packet);
	}
}
