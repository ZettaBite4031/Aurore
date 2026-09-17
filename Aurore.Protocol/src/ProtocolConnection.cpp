#include <Aurore/Protocol/ProtocolConnection.hpp>

#include <utility>

namespace {
	Aurore::Protocol::ProtocolConnectionError FlattenPipelineError(Aurore::Protocol::ProtocolPipelineError error) {
		return std::visit([](const auto value) -> Aurore::Protocol::ProtocolConnectionError { return value; }, error);
	}
}

namespace Aurore::Protocol {
	ProtocolConnection::ProtocolConnection(ServerStatus server_status, ProtocolPipelineConfiguration pipeline_configuration)
		: m_Pipeline(pipeline_configuration), m_Session(std::move(server_status)) {}

	ProtocolConnection::ProtocolConnection(ServerStatus server_status, std::size_t maximum_packet_size)
		: ProtocolConnection(std::move(server_status),
		  ProtocolPipelineConfiguration{
				.MaximumFramedPacketSize = maximum_packet_size,
				.MaximumDecompressedPacketSize = maximum_packet_size,
				.MaximumOutboundActionBytes = maximum_packet_size }) {}

	ProtocolProcessResult ProtocolConnection::Receive(std::span<const std::byte> bytes) {
		ProtocolProcessResult result;
		result.Disposition = m_Disposition;

		if (m_Disposition != ConnectionDisposition::KeepOpen) return result;

		const auto append_state = m_Session.GetState();
		const auto append_result = m_Pipeline.AppendInbound(bytes);

		if (!append_result) {
			Terminate(result, FlattenPipelineError(append_result.error()), MakeFailureContext(ProtocolProcessingStage::AppendInbound, append_state, bytes.size()));
			return result;
		}

		while (true) {
			const auto decode_state = m_Session.GetState();
			auto frame_result = m_Pipeline.TryDecodeInbound();
			if (!frame_result) {
				Terminate(result, FlattenPipelineError(frame_result.error()), MakeFailureContext(ProtocolProcessingStage::DecodeFrame, decode_state, bytes.size()));
				return result;
			}

			if (!frame_result->has_value()) return result;
			const auto& frame = frame_result->value();
			const auto packet_state = m_Session.GetState();
			auto protocol_result = m_Session.HandlePacket(frame);
			if (!protocol_result) {
				Terminate(result, ProtocolConnectionError{ protocol_result.error() }, MakeFailureContext(ProtocolProcessingStage::HandlePacket, packet_state, bytes.size(), frame.PacketId, frame.Payload.size()));
				return result;
			}

			auto action = std::move(*protocol_result);
			const bool disconnect = action.Disconnect;
			const bool require_boundary = action.RequireInboundBoundary;
			const auto action_result = AppendAction(result, std::move(action));
			if (!action_result) {
				Terminate(result, action_result.error(), MakeFailureContext(ProtocolProcessingStage::EncodeOutboundAction, packet_state, bytes.size(), frame.PacketId, frame.Payload.size()));
				return result;
			}

			if (require_boundary) {
				if (!m_Pipeline.Empty()) {
					result.Requests.clear();
					Terminate(result, ProtocolConnectionError{ ProtocolError::UnexpectedPacket }, MakeFailureContext(ProtocolProcessingStage::EnforceInboundBoundary, packet_state, bytes.size(), frame.PacketId, frame.Payload.size()));
				}
				return result;
			}

			if (disconnect) {
				CompleteDisconnect(result);
				return result;
			}
		}
	}

	ProtocolProcessResult ProtocolConnection::ResolveLogin(const LoginResolution& resolution) {
		ProtocolProcessResult result;
		result.Disposition = m_Disposition;
		if (m_Disposition != ConnectionDisposition::KeepOpen) return result;

		const auto resolution_state = m_Session.GetState();
		auto protocol_result = m_Session.ResolveLogin(resolution);
		if (!protocol_result) {
			Terminate(result, ProtocolConnectionError{ protocol_result.error() }, MakeFailureContext(ProtocolProcessingStage::ResolveLogin, resolution_state, 0));
			return result;
		}

		auto action = std::move(*protocol_result);
		const bool disconnect = action.Disconnect;
		const auto action_result = AppendAction(result, std::move(action));
		if (!action_result) {
			Terminate(result, action_result.error(), MakeFailureContext(ProtocolProcessingStage::EncodeOutboundAction, resolution_state, 0));
			return result;
		}
		if (disconnect) CompleteDisconnect(result);
		return result;
	}

	ProtocolProcessResult ProtocolConnection::ResolveConfiguration(ConfigurationResolution resolution) {
		ProtocolProcessResult result;
		result.Disposition = m_Disposition;

		if (m_Disposition != ConnectionDisposition::KeepOpen)
			return result;

		const auto resolution_state = m_Session.GetState();
		auto protocol_result = m_Session.ResolveConfiguration(std::move(resolution));
		if (!protocol_result) {
			Terminate(result, ProtocolConnectionError{ protocol_result.error() }, MakeFailureContext(ProtocolProcessingStage::ResolveConfiguration, resolution_state, 0));
			return result;
		}

		auto action = std::move(*protocol_result);
		const bool disconnect = action.Disconnect;
		const auto action_result = AppendAction(result, std::move(action));
		if (!action_result) {
			Terminate(result, action_result.error(), MakeFailureContext(ProtocolProcessingStage::EncodeOutboundAction, resolution_state, 0));
			return result;
		}

		if (disconnect) CompleteDisconnect(result);
		return result;
	}

	const ProtocolSession& ProtocolConnection::GetSession() const noexcept {
		return m_Session;
	}

	ConnectionDisposition ProtocolConnection::GetDisposition() const noexcept {
		return m_Disposition;
	}

	std::size_t ProtocolConnection::BufferedBytes() const noexcept {
		return m_Pipeline.BufferedInboundBytes();
	}

	bool ProtocolConnection::IsOpen() const noexcept {
		return m_Disposition == ConnectionDisposition::KeepOpen;
	}

	bool ProtocolConnection::IsClosing() const noexcept {
		return m_Disposition != ConnectionDisposition::KeepOpen;
	}

	ProtocolConnection::AppendActionResult ProtocolConnection::AppendAction(ProtocolProcessResult& result, ProtocolAction&& action) {
		/*
			Transform into temporary action ownership first. A failure must not
			publish a partial action or expose a Core request whose protocol
			response could not be represented.

			Only the aggregate and the currently encoded frame coexist. This
			avoids retaining every independently encoded packet and then copying
			the entire action into a second allocation.
		*/
		Aurore::Util::ByteBuffer outbound_batch;
		const auto maximum_action_bytes = m_Pipeline.GetConfiguration().MaximumOutboundActionBytes;

		for (auto& packet : action.OutboundPackets) {
			auto packet_frame = EncodeClientboundPacket(std::move(packet));
			auto encoded_frame = m_Pipeline.EncodeOutbound(packet_frame);
			if (!encoded_frame)
				return std::unexpected(FlattenPipelineError(encoded_frame.error()));

			const auto encoded_size = encoded_frame->Size();
			if (encoded_size > maximum_action_bytes
				|| outbound_batch.Size() > maximum_action_bytes - encoded_size) {

				return std::unexpected(ProtocolConnectionError{
					ProtocolTransformError::OutboundActionTooLarge
				});
			}

			outbound_batch.WriteBytes(encoded_frame->Bytes());
		}

		/*
			Only publish requests and output after the complete action has been
			transformed and assembled.
		*/
		result.Requests.reserve(result.Requests.size() + action.Requests.size());

		for (auto& request : action.Requests)
			result.Requests.emplace_back(std::move(request));

		if (!outbound_batch.Empty()) {
			result.OutboundFrames.reserve(result.OutboundFrames.size() + 1);
			result.OutboundFrames.emplace_back(std::move(outbound_batch));
		}

		return {};
	}

	void ProtocolConnection::CompleteDisconnect(ProtocolProcessResult& result) {
		m_Session.Disconnect();
		m_Pipeline.Reset();

		m_Disposition = ConnectionDisposition::CloseAfterFlush;
		result.Disposition = m_Disposition;
	}

	ProtocolFailureContext ProtocolConnection::MakeFailureContext(ProtocolProcessingStage stage, ProtocolState state, std::size_t input_bytes, std::optional<std::int32_t> packet_id, std::size_t packet_payload_bytes) const noexcept {
		return ProtocolFailureContext{
			.Stage = stage,
			.State = state,
			.PacketId = packet_id,
			.PacketPayloadBytes = packet_payload_bytes,
			.InputBytes = input_bytes,
			.BufferedInboundBytes = m_Pipeline.BufferedInboundBytes(),
		};
	}

	void ProtocolConnection::Terminate(ProtocolProcessResult& result, ProtocolConnectionError error, ProtocolFailureContext context) {
		result.Requests.clear();

		m_Pipeline.Reset();
		m_Session.Disconnect();
		/*
			If earlier packets in this batch generated valid responses,
			allow those responses to be transmitted before closing.

			Without pending output, the malformed connection can be closed
			immediately.
		*/
		if (result.OutboundFrames.empty()) {
			m_Disposition = ConnectionDisposition::CloseImmediately;
		}
		else {
			m_Disposition = ConnectionDisposition::CloseAfterFlush;
		}

		result.Disposition = m_Disposition;
		result.Error = std::move(error);
		result.FailureContext = std::move(context);
	}
}

