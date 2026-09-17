#include <Aurore/Protocol/ProtocolDiagnostics.hpp>

#include <cstdint>
#include <format>
#include <type_traits>
#include <variant>

namespace Aurore::Protocol {
	std::string_view GetProtocolStateName(ProtocolState state) noexcept {
		switch (state) {
		case ProtocolState::Handshake: return "Handshake";
		case ProtocolState::Status: return "Status";
		case ProtocolState::Login: return "Login";
		case ProtocolState::Configuration: return "Configuration";
		case ProtocolState::Play: return "Play";
		case ProtocolState::Disconnected: return "Disconnected";
		}
		return "Unknown";
	}

	std::string_view GetProtocolStageName(ProtocolProcessingStage stage) noexcept {
		switch (stage) {
		case ProtocolProcessingStage::AppendInbound: return "AppendInbound";
		case ProtocolProcessingStage::DecodeFrame: return "DecodeFrame";
		case ProtocolProcessingStage::HandlePacket: return "HandlePacket";
		case ProtocolProcessingStage::EncodeOutboundAction: return "EncodeOutboundAction";
		case ProtocolProcessingStage::EnforceInboundBoundary: return "EnforceInboundBoundary";
		case ProtocolProcessingStage::ResolveLogin: return "ResolveLogin";
		case ProtocolProcessingStage::ResolveConfiguration: return "ResolveConfiguration";
		}
		return "Unknown";
	}

	std::string_view GetFrameErrorName(FrameError error) noexcept {
		switch (error) {
		case FrameError::MalformedLength: return "MalformedLength";
		case FrameError::NegativeLength: return "NegativeLength";
		case FrameError::PacketTooLarge: return "PacketTooLarge";
		case FrameError::MissingPacketId: return "MissingPacketId";
		case FrameError::MalformedPacketId: return "MalformedPacketId";
		}
		return "UnknownFrameError";
	}

	std::string_view GetTransformErrorName(ProtocolTransformError error) noexcept {
		switch (error) {
		case ProtocolTransformError::InvalidCompressionThreshold: return "InvalidCompressionThreshold";
		case ProtocolTransformError::InvalidEncryptionKey: return "InvalidEncryptionKey";
		case ProtocolTransformError::CompressionUnavailable: return "CompressionUnavailable";
		case ProtocolTransformError::EncryptionUnavailable: return "EncryptionUnavailable";
		case ProtocolTransformError::DecompressedPacketTooLarge: return "DecompressedPacketTooLarge";
		case ProtocolTransformError::OutboundPacketTooLarge: return "OutboundPacketTooLarge";
		case ProtocolTransformError::OutboundActionTooLarge: return "OutboundActionTooLarge";
		}
		return "UnknownTransformError";
	}

	std::string_view GetProtocolErrorName(ProtocolError error) noexcept {
		switch (error) {
		case ProtocolError::UnexpectedPacket: return "UnexpectedPacket";
		case ProtocolError::MalformedPacket: return "MalformedPacket";
		case ProtocolError::InvalidNextState: return "InvalidNextState";
		case ProtocolError::TrailingPacketData: return "TrailingPacketData";
		case ProtocolError::UnsupportedState: return "UnsupportedState";
		case ProtocolError::InvalidRequestResolution: return "InvalidRequestResolution";
		case ProtocolError::InvalidKnownPackSelection: return "InvalidKnownPackSelection";
		case ProtocolError::MissingConfigurationPlan: return "MissingConfigurationPlan";
		case ProtocolError::ConfigurationGenerationMismatch: return "ConfigurationGenerationMismatch";
		}
		return "UnknownProtocolError";
	}

	std::string_view GetProtocolConnectionErrorName(const ProtocolConnectionError& error) noexcept {
		return std::visit([](const auto value) -> std::string_view {
			using Error = std::remove_cvref_t<decltype(value)>;

			if constexpr (std::is_same_v<Error, FrameError>)
				return GetFrameErrorName(value);
			else if constexpr (std::is_same_v<Error, ProtocolTransformError>)
				return GetTransformErrorName(value);
			else {
				static_assert(std::is_same_v<Error, ProtocolError>);
				return GetProtocolErrorName(value);
			}
		}, error);
	}

	std::string FormatPacketId(std::optional<std::int32_t> packet_id) {
		if (!packet_id) return "N/A";
		return std::format("0x{:02X} ({})", static_cast<std::uint32_t>(*packet_id), *packet_id);
	}
}
