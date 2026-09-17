#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Aurore::Protocol::Packets::Handshake {
	enum class Intention : std::int32_t {
		Status = 1,
		Login = 2,
	};

	namespace Serverbound {
		inline constexpr std::int32_t HandshakePacketId{ 0x0 };
		inline constexpr std::size_t MaximumServerAddressBytes{ 256 };

		struct Handshake final {
			std::int32_t ProtocolVersion{ 0 };
			std::string ServerAddress;
			std::uint16_t ServerPort{ 0 };
			Packets::Handshake::Intention Intention{ Packets::Handshake::Intention::Status };
		};

		using Packet = std::variant<Handshake>;

		[[nodiscard]] ProtocolResult<Packet> Decode(const PacketFrame& frame);
	}
}
