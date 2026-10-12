#include <Aurore/Protocol/ProtocolDiagnostics.hpp>
#include <Aurore/Protocol/PacketCatalog774.hpp>

#include <array>
#include <cstddef>
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
		case ProtocolError::ConfigurationMaterializationFailed: return "ConfigurationMaterializationFailed";
		case ProtocolError::ConfigurationGenerationMismatch: return "ConfigurationGenerationMismatch";
		case ProtocolError::PlayBootstrapFailed: return "PlayBootstrapFailed";
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

	namespace {
		template<std::size_t N>
		std::optional<std::string_view> Lookup(const std::array<std::string_view, N>& names, std::int32_t packet_id) noexcept {
			if (packet_id < 0 || static_cast<std::size_t>(packet_id) >= N) return std::nullopt;
			return names[static_cast<std::size_t>(packet_id)];
		}
	}

	std::optional<std::string_view> FindPacketName(ProtocolState state, PacketDirection direction, std::int32_t packet_id) noexcept {
		using namespace Catalog774;
		switch (state) {
		case ProtocolState::Handshake: return direction == PacketDirection::Serverbound ? Lookup(HandshakeServerbound, packet_id) : std::nullopt;
		case ProtocolState::Status: return direction == PacketDirection::Serverbound ? Lookup(StatusServerbound, packet_id) : Lookup(StatusClientbound, packet_id);
		case ProtocolState::Login: return direction == PacketDirection::Serverbound ? Lookup(LoginServerbound, packet_id) : Lookup(LoginClientbound, packet_id);
		case ProtocolState::Configuration: return direction == PacketDirection::Serverbound ? Lookup(ConfigurationServerbound, packet_id) : Lookup(ConfigurationClientbound, packet_id);
		case ProtocolState::Play: return direction == PacketDirection::Serverbound ? Lookup(Serverbound, packet_id) : Lookup(Clientbound, packet_id);
		case ProtocolState::Disconnected: return std::nullopt;
		}
		return std::nullopt;
	}

	std::string FormatPacketName(ProtocolState state, PacketDirection direction, std::optional<std::int32_t> packet_id) {
		if (!packet_id) return "N/A";
		const auto name = FindPacketName(state, direction, *packet_id);
		if (!name) return FormatPacketId(packet_id);
		return std::format("{} (0x{:02X}; {})", *name, static_cast<std::uint32_t>(*packet_id), *packet_id);
	}

	std::string FormatPacketId(std::optional<std::int32_t> packet_id) {
		if (!packet_id) return "N/A";
		const auto numeric = std::format("0x{:02X} ({})", static_cast<std::uint32_t>(*packet_id), *packet_id);

		// Existing Core log sites pass only the ID, not the protocol state. This
		// is explicitly a candidate (not an assertion about the current state).
		// It makes First Light Play failures readable without coupling Protocol
		// to Core's logging infrastructure or rewriting Core for this milestone.
		if (const auto name = FindPacketName(ProtocolState::Play, PacketDirection::Serverbound, *packet_id))
			return std::format("{} [Play/Serverbound candidate: {}]", numeric, *name);

		return numeric;
	}
}
