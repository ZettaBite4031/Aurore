#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Aurore::Protocol {
	enum class ProtocolCompressionMode : std::uint8_t {
		Disabled,
		Zlib,
	};

	enum class ProtocolEncryptionMode : std::uint8_t {
		Disabled,
		AesCfb8,
	};

	enum class ProtocolTransformError : std::uint8_t {
		InvalidCompressionThreshold,
		InvalidEncryptionKey,
		CompressionUnavailable,
		EncryptionUnavailable,
		DecompressedPacketTooLarge,
		OutboundPacketTooLarge,
		OutboundActionTooLarge,
	};

	using ProtocolPipelineError = std::variant<FrameError, ProtocolTransformError>;

	template<typename T>
	using ProtocolPipelineResult = std::expected<T, ProtocolPipelineError>;

	struct ProtocolPipelineConfiguration final {
		std::size_t MaximumFramedPacketSize{ PacketStreamDecoder::DefaultMaximumPacketSize };
		std::size_t MaximumDecompressedPacketSize{ PacketStreamDecoder::DefaultMaximumPacketSize };

		/*
			Bounds the complete transformed output produced by one ProtocolAction.
			The default intentionally matches Aurore.Network's default retained
			outbound budget per connection.
		*/
		std::size_t MaximumOutboundActionBytes{ PacketStreamDecoder::DefaultMaximumPacketSize };
	};

	struct ProtocolPipelineState final {
		ProtocolCompressionMode Compression{ ProtocolCompressionMode::Disabled };
		std::optional<std::int32_t> CompressionThreshold;

		ProtocolEncryptionMode Encryption{ ProtocolEncryptionMode::Disabled };
	};

	/*
		Owns the complete per-connection byte transformation path.

		Inbound:
			network bytes
			-> decrypt stream stage
			-> outer length framing
			-> decompress packet-data stage
			-> packet ID/payload decode

		Outbound:
			typed PacketFrame
			-> packet ID/payload encode
			-> compress packet-data stage
			-> outer length framing
			-> encrypt stream stage

		Compression and encryption currently fail closed when an enabled mode is
		requested. Their state and exact insertion points are nevertheless real,
		per-connection, and covered by tests, so later codecs do not require a
		ProtocolConnection redesign.
	*/
	class ProtocolPipeline final {
	public:
		explicit ProtocolPipeline(ProtocolPipelineConfiguration config = {});

		ProtocolPipeline(const ProtocolPipeline&) = delete;
		ProtocolPipeline& operator=(const ProtocolPipeline&) = delete;

		ProtocolPipeline(ProtocolPipeline&&) = default;
		ProtocolPipeline& operator=(ProtocolPipeline&&) = default;

		[[nodiscard]] ProtocolPipelineResult<void> AppendInbound(std::span<const std::byte> bytes);
		[[nodiscard]] ProtocolPipelineResult<std::optional<PacketFrame>> TryDecodeInbound();
		[[nodiscard]] ProtocolPipelineResult<Aurore::Util::ByteBuffer> EncodeOutbound(const PacketFrame& frame) const;
		[[nodiscard]] ProtocolPipelineResult<void> ConfigureCompression(ProtocolCompressionMode mode, std::optional<std::int32_t> threshold = std::nullopt);
		[[nodiscard]] ProtocolPipelineResult<void> ConfigureEncryption(ProtocolEncryptionMode mode, std::span<const std::byte> key = {});

		void ClearBufferedData() noexcept;
		void Reset() noexcept;

		[[nodiscard]] std::size_t BufferedInboundBytes() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;

		[[nodiscard]] const ProtocolPipelineConfiguration& GetConfiguration() const noexcept;
		[[nodiscard]] const ProtocolPipelineState& GetState() const noexcept;

	private:
		[[nodiscard]] ProtocolPipelineResult<void> AppendDecryptedInbound(std::span<const std::byte> bytes);
		[[nodiscard]] ProtocolPipelineResult<std::vector<std::byte>> DecompressInbound(std::vector<std::byte> packet_data) const;
		[[nodiscard]] ProtocolPipelineResult<Aurore::Util::ByteBuffer> CompressOutbound(Aurore::Util::ByteBuffer packet_data) const;
		[[nodiscard]] ProtocolPipelineResult<Aurore::Util::ByteBuffer> EncryptOutbound(Aurore::Util::ByteBuffer frame) const;

		ProtocolPipelineConfiguration m_Config;
		ProtocolPipelineState m_State;
		PacketStreamDecoder m_StreamDecoder;
	};
}

