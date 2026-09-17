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
	[[nodiscard]] std::string FormatPacketId(std::optional<std::int32_t> packet_id);
}
