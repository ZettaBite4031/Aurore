#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace Aurore::Tests {
#pragma region ByteBuffer Tests

	// Byte Buffer ----------------------------
	using Aurore::Util::BufferError;
	using Aurore::Util::ByteBuffer;
	using Aurore::Util::ByteReader;
	namespace {
		std::vector<std::byte> MakeBytes(std::initializer_list<std::uint8_t> values) {
			std::vector<std::byte> result;
			result.reserve(values.size());
			for (const auto value : values) {
				result.push_back(static_cast<std::byte>(value));
			}
			return result;
		}

		std::vector<std::uint8_t> ToIntegers(std::span<const std::byte> bytes) {
			std::vector<std::uint8_t> result;
			result.reserve(bytes.size());
			for (const auto byte : bytes) {
				result.push_back(std::to_integer<std::uint8_t>(byte));
			}
			return result;
		}

	}

	TEST(ByteBufferTests, StartsEmpty) {
		ByteBuffer buffer;

		EXPECT_TRUE(buffer.Empty());
		EXPECT_EQ(buffer.Size(), 0);
		EXPECT_TRUE(buffer.Bytes().empty());
	}

	TEST(ByteBufferTests, WritesRawBytes) {
		ByteBuffer buffer;

		buffer.WriteUnsignedByte(0x12);
		buffer.WriteByte(std::byte{ 0x34 });

		const auto additional = MakeBytes({ 0x56, 0x78 });
		buffer.WriteBytes(additional);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{ 0x12, 0x34, 0x56, 0x78 })
		);
	}

	TEST(ByteBufferTests, ClearRemovesWrittenData) {
		ByteBuffer buffer;

		buffer.WriteInt32(42);
		ASSERT_FALSE(buffer.Empty());

		buffer.Clear();

		EXPECT_TRUE(buffer.Empty());
		EXPECT_EQ(buffer.Size(), 0);
	}

	TEST(ByteBufferTests, WritesBooleansAsSingleBytes) {
		ByteBuffer buffer;

		buffer.WriteBool(false);
		buffer.WriteBool(true);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{ 0x00, 0x01 })
		);
	}

	TEST(ByteBufferTests, WritesFixedWidthIntegersInBigEndianOrder) {
		ByteBuffer buffer;

		buffer.WriteInt16(0x1234);
		buffer.WriteInt32(0x12345678);
		buffer.WriteInt64(0x0102030405060708LL);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{
			0x12, 0x34,
				0x12, 0x34, 0x56, 0x78,
				0x01, 0x02, 0x03, 0x04,
				0x05, 0x06, 0x07, 0x08
		})
		);
	}

	TEST(ByteBufferTests, RoundTripsSignedFixedWidthIntegers) {
		ByteBuffer buffer;

		buffer.WriteInt16(std::numeric_limits<std::int16_t>::min());
		buffer.WriteInt16(std::numeric_limits<std::int16_t>::max());
		buffer.WriteInt32(std::numeric_limits<std::int32_t>::min());
		buffer.WriteInt32(std::numeric_limits<std::int32_t>::max());
		buffer.WriteInt64(std::numeric_limits<std::int64_t>::min());
		buffer.WriteInt64(std::numeric_limits<std::int64_t>::max());

		ByteReader reader(buffer.Bytes());

		EXPECT_EQ(reader.ReadInt16().value(), std::numeric_limits<std::int16_t>::min());
		EXPECT_EQ(reader.ReadInt16().value(), std::numeric_limits<std::int16_t>::max());
		EXPECT_EQ(reader.ReadInt32().value(), std::numeric_limits<std::int32_t>::min());
		EXPECT_EQ(reader.ReadInt32().value(), std::numeric_limits<std::int32_t>::max());
		EXPECT_EQ(reader.ReadInt64().value(), std::numeric_limits<std::int64_t>::min());
		EXPECT_EQ(reader.ReadInt64().value(), std::numeric_limits<std::int64_t>::max());
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ByteBufferTests, WritesFloatingPointValuesUsingBigEndianIeeeBits) {
		ByteBuffer buffer;

		buffer.WriteFloat(1.0f);
		buffer.WriteDouble(1.0);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{
			0x3F, 0x80, 0x00, 0x00,
				0x3F, 0xF0, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00
		})
		);
	}

	TEST(ByteBufferTests, RoundTripsFloatingPointBitPatterns) {
		const float floatValue = std::bit_cast<float>(std::uint32_t{ 0xFFC01234 });
		const double doubleValue = std::bit_cast<double>(std::uint64_t{ 0xFFF8000000001234ULL });

		ByteBuffer buffer;

		buffer.WriteFloat(floatValue);
		buffer.WriteDouble(doubleValue);

		ByteReader reader(buffer.Bytes());

		const auto decodedFloat = reader.ReadFloat();
		const auto decodedDouble = reader.ReadDouble();

		ASSERT_TRUE(decodedFloat.has_value());
		ASSERT_TRUE(decodedDouble.has_value());

		EXPECT_EQ(
			std::bit_cast<std::uint32_t>(*decodedFloat),
			std::bit_cast<std::uint32_t>(floatValue)
		);

		EXPECT_EQ(
			std::bit_cast<std::uint64_t>(*decodedDouble),
			std::bit_cast<std::uint64_t>(doubleValue)
		);
	}

	TEST(ByteReaderTests, ReadsNonZeroBooleanAsTrue) {
		const auto data = MakeBytes({ 0x00, 0x01, 0xFF });

		ByteReader reader(data);

		EXPECT_FALSE(reader.ReadBool().value());
		EXPECT_TRUE(reader.ReadBool().value());
		EXPECT_TRUE(reader.ReadBool().value());
	}

	TEST(ByteReaderTests, IncompleteFixedWidthReadDoesNotAdvance) {
		const auto data = MakeBytes({ 0x12, 0x34, 0x56 });

		ByteReader reader(data);

		const auto result = reader.ReadInt32();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::IncompleteData);
		EXPECT_EQ(reader.Position(), 0);
		EXPECT_EQ(reader.Remaining(), 3);
	}

	TEST(ByteReaderTests, ReadBytesReturnsNonOwningSpan) {
		const auto data = MakeBytes({ 0x10, 0x20, 0x30, 0x40 });

		ByteReader reader(data);

		const auto bytes = reader.ReadBytes(3);

		ASSERT_TRUE(bytes.has_value());

		EXPECT_EQ(
			ToIntegers(*bytes),
			(std::vector<std::uint8_t>{ 0x10, 0x20, 0x30 })
		);

		EXPECT_EQ(reader.Position(), 3);
		EXPECT_EQ(reader.Remaining(), 1);
	}

	TEST(ByteReaderTests, IncompleteReadBytesDoesNotAdvance) {
		const auto data = MakeBytes({ 0x10, 0x20 });

		ByteReader reader(data);

		const auto result = reader.ReadBytes(3);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::IncompleteData);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(ByteReaderTests, ResetReturnsReaderToBeginning) {
		const auto data = MakeBytes({ 0x12, 0x34 });

		ByteReader reader(data);

		ASSERT_TRUE(reader.ReadUnsignedByte().has_value());
		EXPECT_EQ(reader.Position(), 1);

		reader.Reset();

		EXPECT_EQ(reader.Position(), 0);
		EXPECT_EQ(reader.ReadUnsignedByte().value(), 0x12);
	}

	TEST(VarIntTests, WritesKnownMinecraftEncodings) {
		struct TestCase {
			std::int32_t Value;
			std::vector<std::uint8_t> Expected;
		};

		const std::array cases{
			TestCase{ 0, { 0x00 } },
			TestCase{ 1, { 0x01 } },
			TestCase{ 127, { 0x7F } },
			TestCase{ 128, { 0x80, 0x01 } },
			TestCase{ 255, { 0xFF, 0x01 } },
			TestCase{ 25565, { 0xDD, 0xC7, 0x01 } },
			TestCase{ 2097151, { 0xFF, 0xFF, 0x7F } },
			TestCase{
				std::numeric_limits<std::int32_t>::max(),
				{ 0xFF, 0xFF, 0xFF, 0xFF, 0x07 }
			},
			TestCase{
				-1,
				{ 0xFF, 0xFF, 0xFF, 0xFF, 0x0F }
			},
			TestCase{
				std::numeric_limits<std::int32_t>::min(),
				{ 0x80, 0x80, 0x80, 0x80, 0x08 }
			}
		};

		for (const auto& testCase : cases) {
			ByteBuffer buffer;
			buffer.WriteVarInt(testCase.Value);

			EXPECT_EQ(ToIntegers(buffer.Bytes()), testCase.Expected)
				<< "Value: " << testCase.Value;
		}
	}

	TEST(VarIntTests, RoundTripsRepresentativeValues) {
		const std::array<std::int32_t, 12> values{
			0,
			1,
			2,
			127,
			128,
			255,
			25565,
			2097151,
			-1,
			-25565,
			std::numeric_limits<std::int32_t>::min(),
			std::numeric_limits<std::int32_t>::max()
		};

		ByteBuffer buffer;

		for (const auto value : values) {
			buffer.WriteVarInt(value);
		}

		ByteReader reader(buffer.Bytes());

		for (const auto expected : values) {
			const auto result = reader.ReadVarInt();

			ASSERT_TRUE(result.has_value());
			EXPECT_EQ(*result, expected);
		}

		EXPECT_TRUE(reader.Empty());
	}

	TEST(VarIntTests, TruncatedVarIntDoesNotAdvance) {
		const auto data = MakeBytes({ 0x80 });

		ByteReader reader(data);

		const auto result = reader.ReadVarInt();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::IncompleteData);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(VarIntTests, OverlongVarIntDoesNotAdvance) {
		const auto data = MakeBytes({
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x00
			});

		ByteReader reader(data);

		const auto result = reader.ReadVarInt();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::VarIntTooLong);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(VarLongTests, WritesNegativeOneUsingTenBytes) {
		ByteBuffer buffer;

		buffer.WriteVarLong(-1);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				0xFF, 0xFF, 0xFF, 0xFF, 0x01
		})
		);
	}

	TEST(VarLongTests, RoundTripsRepresentativeValues) {
		const std::array<std::int64_t, 10> values{
			0,
			1,
			127,
			128,
			25565,
			2097151,
			-1,
			-25565,
			std::numeric_limits<std::int64_t>::min(),
			std::numeric_limits<std::int64_t>::max()
		};

		ByteBuffer buffer;

		for (const auto value : values) {
			buffer.WriteVarLong(value);
		}

		ByteReader reader(buffer.Bytes());

		for (const auto expected : values) {
			const auto result = reader.ReadVarLong();

			ASSERT_TRUE(result.has_value());
			EXPECT_EQ(*result, expected);
		}

		EXPECT_TRUE(reader.Empty());
	}

	TEST(VarLongTests, OverlongVarLongDoesNotAdvance) {
		const auto data = MakeBytes({
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x80,
			0x00
			});

		ByteReader reader(data);

		const auto result = reader.ReadVarLong();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::VarLongTooLong);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(VarIntTests, RejectsFifthBytePayloadOutside32BitRange) {
		const auto data = MakeBytes({ 0xFF, 0xFF, 0xFF, 0xFF, 0x10 });
		ByteReader reader(data);
		const auto result = reader.ReadVarInt();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::VarIntTooLong);
		EXPECT_EQ(reader.Position(), 0);
		EXPECT_EQ(reader.Remaining(), data.size());
	}

	TEST(StringTests, WritesMinecraftLengthPrefixedString) {
		ByteBuffer buffer;

		buffer.WriteString("Hello");

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{
			0x05,
				'H',
				'e',
				'l',
				'l',
				'o'
		})
		);
	}

	TEST(VarIntTests, AcceptsMaximumValidFifthBytePayload) {
		const auto data = MakeBytes({
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0x0F
			});

		ByteReader reader(data);

		const auto result = reader.ReadVarInt();

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(*result, -1);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(StringTests, RoundTripsEmptyAndUtf8Strings) {
		const std::string utf8Snowman = "\xE2\x98\x83";

		ByteBuffer buffer;

		buffer.WriteString("");
		buffer.WriteString(utf8Snowman);

		ByteReader reader(buffer.Bytes());

		const auto empty = reader.ReadString(0);
		const auto snowman = reader.ReadString(utf8Snowman.size());

		ASSERT_TRUE(empty.has_value());
		ASSERT_TRUE(snowman.has_value());

		EXPECT_TRUE(empty->empty());
		EXPECT_EQ(*snowman, utf8Snowman);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(StringTests, RejectsNegativeLengthWithoutAdvancing) {
		const auto data = MakeBytes({
			0xFF,
			0xFF,
			0xFF,
			0xFF,
			0x0F
			});

		ByteReader reader(data);

		const auto result = reader.ReadString(32767);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::NegativeLength);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(StringTests, RejectsLengthOverConfiguredLimitWithoutAdvancing) {
		const auto data = MakeBytes({
			0x05,
			'H',
			'e',
			'l',
			'l',
			'o'
			});

		ByteReader reader(data);

		const auto result = reader.ReadString(4);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::LengthLimitExceeded);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(StringTests, RejectsTruncatedPayloadWithoutAdvancing) {
		const auto data = MakeBytes({
			0x05,
			'H',
			'e'
			});

		ByteReader reader(data);

		const auto result = reader.ReadString(32);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::IncompleteData);
		EXPECT_EQ(reader.Position(), 0);
	}

	TEST(StringTests, SuccessfulReadAdvancesPastLengthAndPayload) {
		ByteBuffer buffer;

		buffer.WriteString("abc");
		buffer.WriteUnsignedByte(0x7F);

		ByteReader reader(buffer.Bytes());

		const auto string = reader.ReadString(3);

		ASSERT_TRUE(string.has_value());
		EXPECT_EQ(*string, "abc");
		EXPECT_EQ(reader.Position(), 4);
		EXPECT_EQ(reader.ReadUnsignedByte().value(), 0x7F);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ByteBufferTests, WritesUInt16InBigEndianOrder) {
		ByteBuffer buffer;

		buffer.WriteUInt16(0xABCD);

		EXPECT_EQ(
			ToIntegers(buffer.Bytes()),
			(std::vector<std::uint8_t>{ 0xAB, 0xCD })
		);
	}

	TEST(ByteReaderTests, ReadsUInt16InBigEndianOrder) {
		const auto data = MakeBytes({ 0xAB, 0xCD });

		ByteReader reader(data);

		const auto result = reader.ReadUInt16();

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(*result, 0xABCD);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ByteReaderTests, IncompleteUInt16ReadDoesNotAdvance) {
		const auto data = MakeBytes({ 0xAB });

		ByteReader reader(data);

		const auto result = reader.ReadUInt16();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), BufferError::IncompleteData);
		EXPECT_EQ(reader.Position(), 0);
		EXPECT_EQ(reader.Remaining(), 1);
	}
#pragma endregion
}
