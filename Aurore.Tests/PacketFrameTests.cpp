#include <Aurore/Protocol/PacketFrame.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <vector>

namespace Aurore::Tests {
namespace {
	std::vector<std::byte> MakeBytes(std::initializer_list<std::uint8_t> values) {
		std::vector<std::byte> result;
		result.reserve(values.size());
		for (const auto value : values)
			result.push_back(static_cast<std::byte>(value));
		return result;
	}

	std::vector<std::uint8_t> ToIntegers(std::span<const std::byte> bytes) {
		std::vector<std::uint8_t> result;
		result.reserve(bytes.size());
		for (const auto byte : bytes)
			result.push_back(std::to_integer<std::uint8_t>(byte));
		return result;
	}

	void AppendBytes(std::vector<std::byte>& destination, std::span<const std::byte> source) {
		destination.insert(destination.end(), source.begin(), source.end());
	}
}

#pragma region Packet Frame Tests
	using Aurore::Protocol::EncodePacketFrame;
	using Aurore::Protocol::FrameError;
	using Aurore::Protocol::PacketFrameDecoder;

	TEST(PacketFrameEncodingTests, EncodesPacketIdAndPayload) {
		const auto payload = MakeBytes({ 0xAA, 0xBB });

		const auto frame = EncodePacketFrame(0, payload);

		EXPECT_EQ(
			ToIntegers(frame.Bytes()),
			(std::vector<std::uint8_t>{ 0x03, 0x00, 0xAA, 0xBB })
		);
	}

	TEST(PacketFrameEncodingTests, IncludesMultiBytePacketIdInPacketLength) {
		const auto frame = EncodePacketFrame(300, {});

		EXPECT_EQ(
			ToIntegers(frame.Bytes()),
			(std::vector<std::uint8_t>{ 0x02, 0xAC, 0x02 })
		);
	}

	TEST(PacketFrameDecoderTests, RejectsZeroMaximumPacketSize) {
		EXPECT_THROW(
			PacketFrameDecoder(0),
			std::invalid_argument
		);
	}

	TEST(PacketFrameDecoderTests, StartsEmpty) {
		PacketFrameDecoder decoder;

		EXPECT_TRUE(decoder.Empty());
		EXPECT_EQ(decoder.BufferedBytes(), 0);
		EXPECT_EQ(
			decoder.GetMaximumPacketSize(),
			PacketFrameDecoder::DefaultMaximumPacketSize
		);
	}

	TEST(PacketFrameDecoderTests, ReturnsNoFrameWhenEmpty) {
		PacketFrameDecoder decoder;

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		EXPECT_FALSE(result->has_value());
	}

	TEST(PacketFrameDecoderTests, DecodesCompleteFrame) {
		const auto payload = MakeBytes({ 0xAA, 0xBB });
		const auto encoded = EncodePacketFrame(0, payload);

		PacketFrameDecoder decoder;
		decoder.Append(encoded.Bytes());

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(result->has_value());

		const auto& frame = result->value();

		EXPECT_EQ(frame.PacketId, 0);
		EXPECT_EQ(ToIntegers(frame.Payload), ToIntegers(payload));
		EXPECT_TRUE(decoder.Empty());
		EXPECT_EQ(decoder.BufferedBytes(), 0);
	}

	TEST(PacketFrameDecoderTests, DecodesFrameWithEmptyPayload) {
		const auto encoded = EncodePacketFrame(5, {});

		PacketFrameDecoder decoder;
		decoder.Append(encoded.Bytes());

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(result->has_value());

		const auto& frame = result->value();

		EXPECT_EQ(frame.PacketId, 5);
		EXPECT_TRUE(frame.Payload.empty());
		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, WaitsForSplitLengthVarInt) {
		const std::vector<std::byte> payload(127, std::byte{ 0x2A });
		const auto encoded = EncodePacketFrame(0, payload);

		ASSERT_GE(encoded.Size(), 2);
		ASSERT_EQ(std::to_integer<std::uint8_t>(encoded.Bytes()[0]), 0x80);
		ASSERT_EQ(std::to_integer<std::uint8_t>(encoded.Bytes()[1]), 0x01);

		PacketFrameDecoder decoder;
		decoder.Append(encoded.Bytes().first(1));

		const auto incomplete = decoder.TryDecode();

		ASSERT_TRUE(incomplete.has_value());
		EXPECT_FALSE(incomplete->has_value());
		EXPECT_EQ(decoder.BufferedBytes(), 1);

		decoder.Append(encoded.Bytes().subspan(1));

		const auto complete = decoder.TryDecode();

		ASSERT_TRUE(complete.has_value());
		ASSERT_TRUE(complete->has_value());
		EXPECT_EQ(complete->value().PacketId, 0);
		EXPECT_EQ(complete->value().Payload, payload);
		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, WaitsForSplitPacketBody) {
		const auto payload = MakeBytes({ 0xAA, 0xBB });
		const auto encoded = EncodePacketFrame(0, payload);

		PacketFrameDecoder decoder;
		decoder.Append(encoded.Bytes().first(3));

		const auto incomplete = decoder.TryDecode();

		ASSERT_TRUE(incomplete.has_value());
		EXPECT_FALSE(incomplete->has_value());
		EXPECT_EQ(decoder.BufferedBytes(), 3);

		decoder.Append(encoded.Bytes().subspan(3));

		const auto complete = decoder.TryDecode();

		ASSERT_TRUE(complete.has_value());
		ASSERT_TRUE(complete->has_value());
		EXPECT_EQ(complete->value().PacketId, 0);
		EXPECT_EQ(ToIntegers(complete->value().Payload), ToIntegers(payload));
		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, DecodesPacketAppendedOneByteAtATime) {
		const auto payload = MakeBytes({ 0x10, 0x20, 0x30, 0x40 });
		const auto encoded = EncodePacketFrame(127, payload);

		PacketFrameDecoder decoder;

		for (std::size_t index = 0; index < encoded.Size(); ++index) {
			decoder.Append(encoded.Bytes().subspan(index, 1));

			const auto result = decoder.TryDecode();

			ASSERT_TRUE(result.has_value());

			if (index + 1 < encoded.Size()) {
				EXPECT_FALSE(result->has_value());
				continue;
			}

			ASSERT_TRUE(result->has_value());
			EXPECT_EQ(result->value().PacketId, 127);
			EXPECT_EQ(ToIntegers(result->value().Payload), ToIntegers(payload));
		}

		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, DecodesMultipleFramesFromSingleAppend) {
		const auto firstPayload = MakeBytes({ 0xAA });
		const auto secondPayload = MakeBytes({ 0xBB, 0xCC });
		const auto first = EncodePacketFrame(1, firstPayload);
		const auto second = EncodePacketFrame(2, secondPayload);

		std::vector<std::byte> combined;
		AppendBytes(combined, first.Bytes());
		AppendBytes(combined, second.Bytes());

		PacketFrameDecoder decoder;
		decoder.Append(combined);

		const auto firstResult = decoder.TryDecode();
		const auto secondResult = decoder.TryDecode();
		const auto emptyResult = decoder.TryDecode();

		ASSERT_TRUE(firstResult.has_value());
		ASSERT_TRUE(firstResult->has_value());
		EXPECT_EQ(firstResult->value().PacketId, 1);
		EXPECT_EQ(ToIntegers(firstResult->value().Payload), ToIntegers(firstPayload));

		ASSERT_TRUE(secondResult.has_value());
		ASSERT_TRUE(secondResult->has_value());
		EXPECT_EQ(secondResult->value().PacketId, 2);
		EXPECT_EQ(ToIntegers(secondResult->value().Payload), ToIntegers(secondPayload));

		ASSERT_TRUE(emptyResult.has_value());
		EXPECT_FALSE(emptyResult->has_value());
		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, PreservesPartialFrameAfterCompleteFrame) {
		const auto firstPayload = MakeBytes({ 0xAA });
		const auto secondPayload = MakeBytes({ 0xBB, 0xCC });
		const auto first = EncodePacketFrame(1, firstPayload);
		const auto second = EncodePacketFrame(2, secondPayload);

		std::vector<std::byte> initial;
		AppendBytes(initial, first.Bytes());
		AppendBytes(initial, second.Bytes().first(2));

		PacketFrameDecoder decoder;
		decoder.Append(initial);

		const auto firstResult = decoder.TryDecode();

		ASSERT_TRUE(firstResult.has_value());
		ASSERT_TRUE(firstResult->has_value());
		EXPECT_EQ(firstResult->value().PacketId, 1);

		const auto incompleteResult = decoder.TryDecode();

		ASSERT_TRUE(incompleteResult.has_value());
		EXPECT_FALSE(incompleteResult->has_value());
		EXPECT_EQ(decoder.BufferedBytes(), 2);

		decoder.Append(second.Bytes().subspan(2));

		const auto secondResult = decoder.TryDecode();

		ASSERT_TRUE(secondResult.has_value());
		ASSERT_TRUE(secondResult->has_value());
		EXPECT_EQ(secondResult->value().PacketId, 2);
		EXPECT_EQ(ToIntegers(secondResult->value().Payload), ToIntegers(secondPayload));
		EXPECT_TRUE(decoder.Empty());
	}

	TEST(PacketFrameDecoderTests, RejectsZeroLengthPacketBody) {
		const auto data = MakeBytes({ 0x00 });

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::MissingPacketId);
	}

	TEST(PacketFrameDecoderTests, RejectsNegativePacketLength) {
		const auto data = MakeBytes({
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0x0F
			});

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::NegativeLength);
	}

	TEST(PacketFrameDecoderTests, RejectsOverlongPacketLengthVarInt) {
		const auto data = MakeBytes({
			0x80,
			0x80,
			0x80,
			0x80,
			0x80
			});

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::MalformedLength);
	}

	TEST(PacketFrameDecoderTests, RejectsPacketLengthWithInvalidFifthByte) {
		const auto data = MakeBytes({
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0x10
			});

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::MalformedLength);
	}

	TEST(PacketFrameDecoderTests, RejectsPacketLargerThanConfiguredLimit) {
		const auto data = MakeBytes({ 0x04 });

		PacketFrameDecoder decoder(3);
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::PacketTooLarge);
	}

	TEST(PacketFrameDecoderTests, AcceptsPacketAtConfiguredSizeLimit) {
		const auto payload = MakeBytes({ 0xAA, 0xBB, 0xCC });
		const auto encoded = EncodePacketFrame(0, payload);

		PacketFrameDecoder decoder(4);
		decoder.Append(encoded.Bytes());

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(result->has_value());
		EXPECT_EQ(result->value().PacketId, 0);
		EXPECT_EQ(ToIntegers(result->value().Payload), ToIntegers(payload));
	}

	TEST(PacketFrameDecoderTests, RejectsIncompletePacketIdInsideCompleteBody) {
		const auto data = MakeBytes({
			0x01,
			0x80
			});

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::MalformedPacketId);
	}

	TEST(PacketFrameDecoderTests, RejectsOverlongPacketId) {
		const auto data = MakeBytes({
			0x06,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x00
			});

		PacketFrameDecoder decoder;
		decoder.Append(data);

		const auto result = decoder.TryDecode();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), FrameError::MalformedPacketId);
	}

	TEST(PacketFrameDecoderTests, ClearRemovesBufferedData) {
		const auto data = MakeBytes({ 0x03, 0x00 });

		PacketFrameDecoder decoder;
		decoder.Append(data);

		ASSERT_FALSE(decoder.Empty());
		ASSERT_EQ(decoder.BufferedBytes(), 2);

		decoder.Clear();

		EXPECT_TRUE(decoder.Empty());
		EXPECT_EQ(decoder.BufferedBytes(), 0);

		const auto result = decoder.TryDecode();

		ASSERT_TRUE(result.has_value());
		EXPECT_FALSE(result->has_value());
	}

	TEST(PacketFrameDecoderTests, CompactionDoesNotLoseBufferedFrames) {
		const auto encoded = EncodePacketFrame(0, {});
		constexpr std::size_t FrameCount = 3000;

		std::vector<std::byte> combined;
		combined.reserve(encoded.Size() * FrameCount);

		for (std::size_t index = 0; index < FrameCount; ++index) {
			AppendBytes(combined, encoded.Bytes());
		}

		PacketFrameDecoder decoder;
		decoder.Append(combined);

		for (std::size_t index = 0; index < FrameCount; ++index) {
			const auto result = decoder.TryDecode();

			ASSERT_TRUE(result.has_value());
			ASSERT_TRUE(result->has_value());
			EXPECT_EQ(result->value().PacketId, 0);
			EXPECT_TRUE(result->value().Payload.empty());
		}

		EXPECT_TRUE(decoder.Empty());
		EXPECT_EQ(decoder.BufferedBytes(), 0);
	}

#pragma endregion
}
