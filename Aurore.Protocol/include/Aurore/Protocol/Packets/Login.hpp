#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/Util/UUID.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Aurore::Protocol::Packets::Login {
	namespace Serverbound {
		inline constexpr std::int32_t StartPacketId{ 0x00 };
		inline constexpr std::int32_t AcknowledgedPacketId{ 0x03 };

		/*
			Valid Java Edition player names use a single-byte ASCII subset.
			Semantic validation remains a Core responsibility
		*/
		inline constexpr std::size_t MaximumUsernameEncodedBytes{ 16 };

		struct Start final {
			std::string Username;
			Aurore::Util::Uuid PresentedProfileId;
		};

		struct Acknowledged final {};

		using Packet = std::variant<Start, Acknowledged>;

		[[nodiscard]] ProtocolResult<Packet> Decode(const PacketFrame& frame);
	}

	namespace Clientbound {
		inline constexpr std::int32_t DisconnectPacketId{ 0x00 };
		inline constexpr std::int32_t SuccessPacketId{ 0x02 };

		struct Disconnect final { std::string ReasonJson; };

		struct Success final {
			Aurore::Util::Uuid ProfileId;
			std::string Username;

			std::vector<ProfileProperty> Properties;
		};

		using Packet = std::variant<Disconnect, Success>;

		[[nodiscard]] PacketFrame Encode(const Disconnect& packet);
		[[nodiscard]] PacketFrame Encode(const Success& packet);
		[[nodiscard]] PacketFrame Encode(const Packet& packet);
	}
}
