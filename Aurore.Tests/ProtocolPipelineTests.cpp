#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolPipeline.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Protocol::DecodePacketData;
		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::FrameError;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PacketFrameDecoder;
		using Aurore::Protocol::ProtocolCompressionMode;
		using Aurore::Protocol::ProtocolEncryptionMode;
		using Aurore::Protocol::ProtocolPipeline;
		using Aurore::Protocol::ProtocolPipelineConfiguration;
		using Aurore::Protocol::ProtocolTransformError;
		using Aurore::Util::ByteBuffer;

		std::span<const std::byte> PayloadBytes(
			const PacketFrame& frame) noexcept {

			return {
				frame.Payload.data(),
				frame.Payload.size(),
			};
		}
	}

	TEST(ProtocolPipelineTests, DisabledPipelinePreservesLegacyEncoding) {
		ByteBuffer payload;
		payload.WriteString("pipeline");
		payload.WriteInt32(42);

		const PacketFrame frame{
			.PacketId = 0x27,
			.Payload = std::vector<std::byte>(
				payload.Bytes().begin(),
				payload.Bytes().end()),
		};

		ProtocolPipeline pipeline;

		const auto encoded =
			pipeline.EncodeOutbound(frame);

		ASSERT_TRUE(encoded.has_value());

		const auto legacy = EncodePacketFrame(
			frame.PacketId,
			PayloadBytes(frame));

		EXPECT_TRUE(std::ranges::equal(
			encoded->Bytes(),
			legacy.Bytes()));

		EXPECT_EQ(
			pipeline.GetState().Compression,
			ProtocolCompressionMode::Disabled);

		EXPECT_EQ(
			pipeline.GetState().Encryption,
			ProtocolEncryptionMode::Disabled);
	}

	TEST(ProtocolPipelineTests, DecodesInboundOneByteAtATime) {
		ByteBuffer payload;
		payload.WriteVarInt(774);
		payload.WriteString("localhost");

		const auto encoded = EncodePacketFrame(
			0x00,
			payload.Bytes());

		ProtocolPipeline pipeline;

		for (std::size_t index{ 0 };
			index < encoded.Size();
			++index) {

			const auto append_result =
				pipeline.AppendInbound(
					encoded.Bytes().subspan(index, 1));

			ASSERT_TRUE(append_result.has_value());

			const auto decode_result =
				pipeline.TryDecodeInbound();

			ASSERT_TRUE(decode_result.has_value());

			if (index + 1 < encoded.Size()) {
				EXPECT_FALSE(
					decode_result->has_value());
				continue;
			}

			ASSERT_TRUE(
				decode_result->has_value());

			EXPECT_EQ(
				decode_result->value().PacketId,
				0x00);

			EXPECT_TRUE(std::ranges::equal(
				decode_result->value().Payload,
				payload.Bytes()));
		}

		EXPECT_TRUE(pipeline.Empty());
	}

	TEST(ProtocolPipelineTests, PreservesCoalescedPacketBoundaries) {
		const auto first = EncodePacketFrame(
			0x01,
			std::span<const std::byte>{});

		ByteBuffer second_payload;
		second_payload.WriteInt64(123456789);

		const auto second = EncodePacketFrame(
			0x02,
			second_payload.Bytes());

		std::vector<std::byte> received;
		received.insert(
			received.end(),
			first.Bytes().begin(),
			first.Bytes().end());

		received.insert(
			received.end(),
			second.Bytes().begin(),
			second.Bytes().end());

		ProtocolPipeline pipeline;

		ASSERT_TRUE(
			pipeline.AppendInbound(received));

		const auto first_result =
			pipeline.TryDecodeInbound();

		ASSERT_TRUE(first_result.has_value());
		ASSERT_TRUE(first_result->has_value());
		EXPECT_EQ(first_result->value().PacketId, 0x01);

		const auto second_result =
			pipeline.TryDecodeInbound();

		ASSERT_TRUE(second_result.has_value());
		ASSERT_TRUE(second_result->has_value());
		EXPECT_EQ(second_result->value().PacketId, 0x02);

		EXPECT_TRUE(std::ranges::equal(
			second_result->value().Payload,
			second_payload.Bytes()));

		const auto empty_result =
			pipeline.TryDecodeInbound();

		ASSERT_TRUE(empty_result.has_value());
		EXPECT_FALSE(empty_result->has_value());
	}

	TEST(ProtocolPipelineTests, EnforcesDecompressedLimitAfterFraming) {
		ProtocolPipeline pipeline{
			ProtocolPipelineConfiguration{
				.MaximumFramedPacketSize = 64,
				.MaximumDecompressedPacketSize = 4,
			}
		};

		const std::array<std::byte, 4> payload{
			std::byte{ 0x01 },
			std::byte{ 0x02 },
			std::byte{ 0x03 },
			std::byte{ 0x04 },
		};

		/* Packet ID + payload is five decompressed bytes. */
		const auto encoded = EncodePacketFrame(
			0x00,
			payload);

		ASSERT_TRUE(
			pipeline.AppendInbound(encoded.Bytes()));

		const auto result =
			pipeline.TryDecodeInbound();

		ASSERT_FALSE(result.has_value());
		ASSERT_TRUE(
			std::holds_alternative<
				ProtocolTransformError>(
					result.error()));

		EXPECT_EQ(
			std::get<ProtocolTransformError>(
				result.error()),
			ProtocolTransformError::
				DecompressedPacketTooLarge);
	}

	TEST(ProtocolPipelineTests, EnforcesFramedLimitBeforePacketDecode) {
		ProtocolPipeline pipeline{
			ProtocolPipelineConfiguration{
				.MaximumFramedPacketSize = 4,
				.MaximumDecompressedPacketSize = 64,
			}
		};

		const std::array<std::byte, 4> payload{
			std::byte{ 0x01 },
			std::byte{ 0x02 },
			std::byte{ 0x03 },
			std::byte{ 0x04 },
		};

		const auto encoded = EncodePacketFrame(
			0x00,
			payload);

		ASSERT_TRUE(
			pipeline.AppendInbound(encoded.Bytes()));

		const auto result =
			pipeline.TryDecodeInbound();

		ASSERT_FALSE(result.has_value());
		ASSERT_TRUE(
			std::holds_alternative<FrameError>(
				result.error()));

		EXPECT_EQ(
			std::get<FrameError>(result.error()),
			FrameError::PacketTooLarge);
	}

	TEST(ProtocolPipelineTests, UnsupportedCompressionFailsWithoutStateMutation) {
		ProtocolPipeline pipeline;

		const auto result =
			pipeline.ConfigureCompression(
				ProtocolCompressionMode::Zlib,
				256);

		ASSERT_FALSE(result.has_value());
		ASSERT_TRUE(
			std::holds_alternative<
				ProtocolTransformError>(
					result.error()));

		EXPECT_EQ(
			std::get<ProtocolTransformError>(
				result.error()),
			ProtocolTransformError::
				CompressionUnavailable);

		EXPECT_EQ(
			pipeline.GetState().Compression,
			ProtocolCompressionMode::Disabled);

		EXPECT_FALSE(
			pipeline.GetState()
				.CompressionThreshold
				.has_value());
	}

	TEST(ProtocolPipelineTests, InvalidCompressionThresholdIsRejected) {
		ProtocolPipeline pipeline;

		const auto result =
			pipeline.ConfigureCompression(
				ProtocolCompressionMode::Zlib,
				-1);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			std::get<ProtocolTransformError>(
				result.error()),
			ProtocolTransformError::
				InvalidCompressionThreshold);
	}

	TEST(ProtocolPipelineTests, UnsupportedEncryptionFailsWithoutStateMutation) {
		ProtocolPipeline pipeline;

		const std::array<std::byte, 16> key{};

		const auto result =
			pipeline.ConfigureEncryption(
				ProtocolEncryptionMode::AesCfb8,
				key);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			std::get<ProtocolTransformError>(
				result.error()),
			ProtocolTransformError::
				EncryptionUnavailable);

		EXPECT_EQ(
			pipeline.GetState().Encryption,
			ProtocolEncryptionMode::Disabled);
	}

	TEST(ProtocolPipelineTests, InvalidEncryptionKeyIsRejected) {
		ProtocolPipeline pipeline;

		const std::array<std::byte, 8> key{};

		const auto result =
			pipeline.ConfigureEncryption(
				ProtocolEncryptionMode::AesCfb8,
				key);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			std::get<ProtocolTransformError>(
				result.error()),
			ProtocolTransformError::
				InvalidEncryptionKey);
	}

	TEST(ProtocolPipelineTests, CompatibilityFrameDecoderUsesSeparatedLayers) {
		ByteBuffer payload;
		payload.WriteString("compatibility");

		const auto encoded = EncodePacketFrame(
			0x05,
			payload.Bytes());

		PacketFrameDecoder decoder;
		decoder.Append(encoded.Bytes());

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(result->has_value());

		EXPECT_EQ(result->value().PacketId, 0x05);
		EXPECT_TRUE(std::ranges::equal(
			result->value().Payload,
			payload.Bytes()));
		EXPECT_TRUE(decoder.Empty());
	}
}

