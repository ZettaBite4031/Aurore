#include <Aurore/Protocol/ProtocolPipeline.hpp>

#include <stdexcept>
#include <utility>

namespace Aurore::Protocol {
	ProtocolPipeline::ProtocolPipeline(ProtocolPipelineConfiguration config)
		: m_Config(config), m_StreamDecoder(config.MaximumFramedPacketSize) {
		if (m_Config.MaximumDecompressedPacketSize == 0)
			throw std::invalid_argument("Maximum decompressed packet size must be greater than zero");
		if (m_Config.MaximumOutboundActionBytes == 0)
			throw std::invalid_argument("Maximum outbound action size must be greater than zero");
	}

	ProtocolPipelineResult<void> ProtocolPipeline::AppendInbound(std::span<const std::byte> bytes) {
		return AppendDecryptedInbound(bytes);
	}

	ProtocolPipelineResult<std::optional<PacketFrame>> ProtocolPipeline::TryDecodeInbound() {
		auto framed_packet_data = m_StreamDecoder.TryDecode();
		if (!framed_packet_data)
			return std::unexpected(ProtocolPipelineError{ framed_packet_data.error() });
		if (!framed_packet_data->has_value())
			return std::optional<PacketFrame>{};

		auto packet_data = DecompressInbound(std::move(framed_packet_data->value()));
		if (!packet_data)
			return std::unexpected(packet_data.error());
		if (packet_data->size() > m_Config.MaximumDecompressedPacketSize)
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::DecompressedPacketTooLarge });

		auto frame = DecodePacketData(*packet_data);
		if (!frame)
			return std::unexpected(ProtocolPipelineError{ frame.error() });
		return std::optional<PacketFrame>{ std::move(*frame) };
	}

	ProtocolPipelineResult<Aurore::Util::ByteBuffer> ProtocolPipeline::EncodeOutbound(const PacketFrame& frame) const {
		auto packet_data = EncodePacketData(frame.PacketId, frame.Payload);
		if (packet_data.Size() > m_Config.MaximumDecompressedPacketSize)
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::DecompressedPacketTooLarge });

		auto transformed_packet_data = CompressOutbound(std::move(packet_data));
		if (!transformed_packet_data)
			return std::unexpected(transformed_packet_data.error());
		if (transformed_packet_data->Size() > m_Config.MaximumFramedPacketSize)
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::OutboundPacketTooLarge });

		auto encoded_frame = EncodeLengthPrefixedPacketData(transformed_packet_data->Bytes());
		return EncryptOutbound(std::move(encoded_frame));
	}

	ProtocolPipelineResult<void> ProtocolPipeline::ConfigureCompression(ProtocolCompressionMode mode, std::optional<std::int32_t> threshold) {
		if (mode == ProtocolCompressionMode::Disabled) {
			m_State.Compression = ProtocolCompressionMode::Disabled;
			m_State.CompressionThreshold.reset();
			return {};
		}

		if (!threshold || *threshold < 0)
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::InvalidCompressionThreshold });

		/*
			Do not mutate state until the zlib transform exists. A failed
			activation attempt must leave the connection in its prior mode.
		*/
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::CompressionUnavailable });
	}

	ProtocolPipelineResult<void> ProtocolPipeline::ConfigureEncryption(ProtocolEncryptionMode mode, std::span<const std::byte> key) {
		if (mode == ProtocolEncryptionMode::Disabled) {
			m_State.Encryption = ProtocolEncryptionMode::Disabled;
			return {};
		}

		if (key.size() != 16)
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::InvalidEncryptionKey });

		/*
			As with compression, enabled state is not committed until the AES
			CFB8 transform is implemented and can own its stream state safely.
		*/
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::EncryptionUnavailable });
	}

	void ProtocolPipeline::ClearBufferedData() noexcept {
		m_StreamDecoder.Clear();
	}

	void ProtocolPipeline::Reset() noexcept {
		m_StreamDecoder.Clear();
		m_State = ProtocolPipelineState{};
	}

	std::size_t ProtocolPipeline::BufferedInboundBytes() const noexcept {
		return m_StreamDecoder.BufferedBytes();
	}

	bool ProtocolPipeline::Empty() const noexcept {
		return m_StreamDecoder.Empty();
	}

	const ProtocolPipelineConfiguration& ProtocolPipeline::GetConfiguration() const noexcept {
		return m_Config;
	}

	const ProtocolPipelineState& ProtocolPipeline::GetState() const noexcept {
		return m_State;
	}

	ProtocolPipelineResult<void> ProtocolPipeline::AppendDecryptedInbound(std::span<const std::byte> bytes) {
		switch (m_State.Encryption) {
		case ProtocolEncryptionMode::Disabled:
			m_StreamDecoder.Append(bytes);
			return {};
		case ProtocolEncryptionMode::AesCfb8:
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::EncryptionUnavailable });
		}
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::EncryptionUnavailable });
	}

	ProtocolPipelineResult<std::vector<std::byte>> ProtocolPipeline::DecompressInbound(std::vector<std::byte> packet_data) const {
		switch (m_State.Compression) {
		case ProtocolCompressionMode::Disabled:
			return packet_data;
		case ProtocolCompressionMode::Zlib:
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::CompressionUnavailable });
		}
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::CompressionUnavailable });
	}

	ProtocolPipelineResult<Aurore::Util::ByteBuffer> ProtocolPipeline::CompressOutbound(Aurore::Util::ByteBuffer packet_data) const {
		switch (m_State.Compression) {
		case ProtocolCompressionMode::Disabled:
			return packet_data;
		case ProtocolCompressionMode::Zlib:
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::CompressionUnavailable });
		}
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::CompressionUnavailable });
	}

	ProtocolPipelineResult<Aurore::Util::ByteBuffer> ProtocolPipeline::EncryptOutbound(Aurore::Util::ByteBuffer frame) const {
		switch (m_State.Encryption) {
		case ProtocolEncryptionMode::Disabled:
			return frame;
		case ProtocolEncryptionMode::AesCfb8:
			return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::EncryptionUnavailable });
		}
		return std::unexpected(ProtocolPipelineError{ ProtocolTransformError::EncryptionUnavailable });
	}
}

