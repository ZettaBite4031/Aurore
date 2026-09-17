#pragma once

#include <Aurore/Protocol/ProtocolTypes.hpp>
#include <Aurore/Protocol/ProtocolPipeline.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Aurore::Protocol {
	enum class ConnectionDisposition : std::uint8_t {
		KeepOpen,
		CloseAfterFlush,
		CloseImmediately
	};

	using ProtocolConnectionError = std::variant<
		FrameError,
		ProtocolTransformError,
		ProtocolError>;

	enum class ProtocolProcessingStage : std::uint8_t {
		AppendInbound,
		DecodeFrame,
		HandlePacket,
		EncodeOutboundAction,
		EnforceInboundBoundary,
		ResolveLogin,
		ResolveConfiguration,
	};

	struct ProtocolFailureContext final {
		ProtocolProcessingStage Stage{ ProtocolProcessingStage::HandlePacket };

		// State before ProtocolConnection terminated and reset the session.
		ProtocolState State{ ProtocolState::Handshake };

		// Present when a complete minecraft packet has already been decoded.
		std::optional<std::int32_t> PacketId;
		std::size_t PacketPayloadBytes{ 0 };

		// Number of transport bytes passed to the current Receive invocation.
		// Resolution failures use zero because they are initiated by Core.
		std::size_t InputBytes{ 0 };

		// Bytes still retained by the inbound pipeline at the point of failure,
		// before ProtocolConnection resets it.
		std::size_t BufferedInboundBytes{ 0 };
	};

	struct ProtocolProcessResult final {
		/*
			Each entry is one transaction action batch. A batch may
			contain multiple independently length-prefixed Minecraft
			packets in exact protocol order.
		*/
		std::vector<Aurore::Util::ByteBuffer> OutboundFrames;
		std::vector<ProtocolRequest> Requests;

		ConnectionDisposition Disposition{ ConnectionDisposition::KeepOpen };

		std::optional<ProtocolConnectionError> Error;
		std::optional<ProtocolFailureContext> FailureContext;

		[[nodiscard]] bool HasError() const noexcept {
			return Error.has_value();
		}
	};

	class ProtocolConnection final {
	public:
		explicit ProtocolConnection(ServerStatus server_status = {}, ProtocolPipelineConfiguration pipeline_config = {});

		/*
			Compatibility overload for existing call sites that configured only
			the original framed-packet limit.
		*/
		ProtocolConnection(ServerStatus server_status, std::size_t maximum_packet_size);

		ProtocolConnection(const ProtocolConnection&) = delete;
		ProtocolConnection& operator=(const ProtocolConnection&) = delete;

		ProtocolConnection(ProtocolConnection&&) = default;
		ProtocolConnection& operator=(ProtocolConnection&&) = default;

		[[nodiscard]] ProtocolProcessResult Receive(std::span<const std::byte> bytes);
		[[nodiscard]] ProtocolProcessResult ResolveLogin(const LoginResolution& resolution);
		[[nodiscard]] ProtocolProcessResult ResolveConfiguration(ConfigurationResolution resolution);
		[[nodiscard]] const ProtocolSession& GetSession() const noexcept;
		[[nodiscard]] ConnectionDisposition GetDisposition() const noexcept;
		[[nodiscard]] std::size_t BufferedBytes() const noexcept;
		[[nodiscard]] bool IsOpen() const noexcept;
		[[nodiscard]] bool IsClosing() const noexcept;

	private:
		using AppendActionResult = std::expected<void, ProtocolConnectionError>;

		[[nodiscard]] AppendActionResult AppendAction(ProtocolProcessResult& result, ProtocolAction&& action);
		void CompleteDisconnect(ProtocolProcessResult& result);
		[[nodiscard]] ProtocolFailureContext MakeFailureContext(ProtocolProcessingStage stage, ProtocolState state, std::size_t input_bytes, std::optional<std::int32_t> packet_id = std::nullopt, std::size_t packet_payload_bytes = 0) const noexcept;
		void Terminate(ProtocolProcessResult& result, ProtocolConnectionError error, ProtocolFailureContext context);


		ProtocolPipeline m_Pipeline;
		ProtocolSession m_Session;

		ConnectionDisposition m_Disposition{ ConnectionDisposition::KeepOpen };
	};
}
