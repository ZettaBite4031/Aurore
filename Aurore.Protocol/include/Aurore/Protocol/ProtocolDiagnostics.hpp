#pragma once

#include <Aurore/Protocol/ProtocolConnection.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Aurore::Protocol {
	[[nodiscard]] std::string_view GetProtocolStateName(ProtocolState state) noexcept;
	[[nodiscard]] std::string_view GetProtocolStageName(ProtocolProcessingStage stage) noexcept;
	[[nodiscard]] std::string_view GetFrameErrorName(FrameError error) noexcept;
	[[nodiscard]] std::string_view GetTransformErrorName(ProtocolTransformError error) noexcept;
	[[nodiscard]] std::string_view GetProtocolErrorName(ProtocolError error) noexcept;
	[[nodiscard]] std::string_view GetProtocolConnectionErrorName(const ProtocolConnectionError& error) noexcept;

	enum class PacketDirection : std::uint8_t { Clientbound, Serverbound };

	// Returning a name does not imply that the corresponding decoder is implemented.
	[[nodiscard]] std::optional<std::string_view> FindPacketName(ProtocolState state, PacketDirection direction, std::int32_t packet_id) noexcept;
	[[nodiscard]] std::string FormatPacketName(ProtocolState state, PacketDirection direction, std::optional<std::int32_t> packet_id);
	[[nodiscard]] std::string FormatPacketId(std::optional<std::int32_t> packet_id);
}
