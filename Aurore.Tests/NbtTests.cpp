#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/NbtBinary.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtDocument;
		using Aurore::Util::NbtLimits;
		using Aurore::Util::NbtList;
		using Aurore::Util::NbtModelErrorCode;
		using Aurore::Util::NbtReadErrorCode;
		using Aurore::Util::NbtReader;
		using Aurore::Util::NbtString;
		using Aurore::Util::NbtTextErrorCode;
		using Aurore::Util::NbtType;
		using Aurore::Util::NbtValue;
		using Aurore::Util::NbtWriteErrorCode;
		using Aurore::Util::NbtWriter;

		std::vector<std::byte> MakeBytes(std::initializer_list<std::uint8_t> values) {
			std::vector<std::byte> result;
			result.reserve(values.size());
			for (const auto value : values) result.push_back(static_cast<std::byte>(value));
			return result;
		}

		void ExpectBytesEqual(std::span<const std::byte> actual, std::span<const std::byte> expected) {
			ASSERT_EQ(actual.size(), expected.size());
			EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
		}

		NbtDocument MakeNestedDocument() {
			auto integer_list = NbtList::Create(NbtType::Int, {
				NbtValue::Int(1),
				NbtValue::Int(-2),
				NbtValue::Int(3),
				}).value();

			NbtCompound child;
			child.Set(NbtString{ u"enabled" }, NbtValue::Byte(1));
			child.Set(NbtString{ u"name" }, NbtValue::String(NbtString{ u"aurore_test:block" }));

			NbtCompound root;
			root.Set(NbtString{ u"byte" }, NbtValue::Byte(-1));
			root.Set(NbtString{ u"short" }, NbtValue::Short(-2));
			root.Set(NbtString{ u"int" }, NbtValue::Int(-3));
			root.Set(NbtString{ u"long" }, NbtValue::Long(-4));
			root.Set(NbtString{ u"float" }, NbtValue::Float(std::bit_cast<float>(0x7FC01234u)));
			root.Set(NbtString{ u"double" }, NbtValue::Double(std::bit_cast<double>(0x7FF8000000001234ull)));
			root.Set(NbtString{ u"bytes" }, NbtValue::ByteArray({ -1, 0, 1 }));
			root.Set(NbtString{ u"string" }, NbtValue::String(NbtString{ std::u16string{ u"A\0B", 3 } }));
			root.Set(NbtString{ u"list" }, NbtValue::List(std::move(integer_list)));
			root.Set(NbtString{ u"compound" }, NbtValue::Compound(std::move(child)));
			root.Set(NbtString{ u"ints" }, NbtValue::IntArray({ -1, 0, 1 }));
			root.Set(NbtString{ u"longs" }, NbtValue::LongArray({ -1, 0, 1 }));
			return NbtDocument{ .Name = NbtString{ u"root" }, .Root = NbtValue::Compound(std::move(root)) };
		}
	}

	TEST(NbtStringTests, ConvertsValidUtf8AndEmbeddedNull) {
		const std::string input{ "A\0B \xF0\x9F\x8C\x85", 8 };
		const auto value = NbtString::FromUtf8(input);

		ASSERT_TRUE(value.has_value());
		const auto output = value->ToUtf8();
		ASSERT_TRUE(output.has_value());
		EXPECT_EQ(*output, input);
	}

	TEST(NbtStringTests, RejectsMalformedUtf8AtExactOffset) {
		const std::string value{ static_cast<char>(0xE2), static_cast<char>(0x28), static_cast<char>(0xA1) };
		const auto result = NbtString::FromUtf8(value);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtTextErrorCode::InvalidUtf8);
		EXPECT_EQ(result.error().Offset, 1);
	}

	TEST(NbtStringTests, ReportsUnpairedSurrogates) {
		const NbtString value{ std::u16string{ static_cast<char16_t>(0xD800) } };
		const auto result = value.ToUtf8();

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtTextErrorCode::UnpairedSurrogate);
		EXPECT_EQ(result.error().Offset, 0);
	}

	TEST(NbtListTests, RejectsMismatchedElementTypes) {
		const auto result = NbtList::Create(NbtType::Int, {
			NbtValue::Int(1),
			NbtValue::Long(2),
			});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtModelErrorCode::MismatchedListElementType);
		EXPECT_EQ(result.error().Index, 1);
	}

	TEST(NbtListTests, AllowsTypedAndEndTypedEmptyLists) {
		const auto end_list = NbtList::Create(NbtType::End);
		const auto int_list = NbtList::Create(NbtType::Int);

		ASSERT_TRUE(end_list.has_value());
		ASSERT_TRUE(int_list.has_value());
		EXPECT_TRUE(end_list->Empty());
		EXPECT_TRUE(int_list->Empty());
		EXPECT_EQ(end_list->GetElementType(), NbtType::End);
		EXPECT_EQ(int_list->GetElementType(), NbtType::Int);
	}

	TEST(NbtCompoundTests, ReplacesDuplicateNamesWithoutChangingOrder) {
		NbtCompound compound;
		compound.Set(NbtString{ u"first" }, NbtValue::Int(1));
		compound.Set(NbtString{ u"second" }, NbtValue::Int(2));
		compound.Set(NbtString{ u"first" }, NbtValue::Int(3));

		ASSERT_EQ(compound.Size(), 2u);
		EXPECT_EQ(compound.GetEntries()[0].Name.GetUtf16(), u"first");
		EXPECT_EQ(compound.GetEntries()[1].Name.GetUtf16(), u"second");
		ASSERT_NE(compound.Find(u"first"), nullptr);
		EXPECT_EQ(*compound.Find(u"first")->AsInt(), 3);
	}

	TEST(NbtValueTests, UsesExactFloatingPointBitEquality) {
		const auto positive_zero = NbtValue::Float(std::bit_cast<float>(0x00000000u));
		const auto negative_zero = NbtValue::Float(std::bit_cast<float>(0x80000000u));
		const auto first_nan = NbtValue::Float(std::bit_cast<float>(0x7FC00001u));
		const auto same_nan = NbtValue::Float(std::bit_cast<float>(0x7FC00001u));
		const auto different_nan = NbtValue::Float(std::bit_cast<float>(0x7FC00002u));

		EXPECT_FALSE(positive_zero.ExactEquals(negative_zero));
		EXPECT_TRUE(first_nan.ExactEquals(same_nan));
		EXPECT_FALSE(first_nan.ExactEquals(different_nan));
	}

	TEST(NbtWriterTests, EncodesExactNamedByteDocument) {
		const NbtDocument document{
			.Name = NbtString{ u"x" },
			.Root = NbtValue::Byte(-5),
		};
		const auto result = NbtWriter::WriteDocument(document);
		const auto expected = MakeBytes({
			0x01,
			0x00, 0x01, 0x78,
			0xFB,
			});

		ASSERT_TRUE(result.has_value());
		ExpectBytesEqual(*result, expected);
	}

	TEST(NbtWriterTests, EncodesModifiedUtf8NullAsTwoBytes) {
		const NbtDocument document{
			.Name = NbtString{},
			.Root = NbtValue::String(NbtString{ std::u16string{ u"A\0B", 3 } }),
		};
		const auto result = NbtWriter::WriteDocument(document);
		const auto expected = MakeBytes({
			0x08,
			0x00, 0x00,
			0x00, 0x04,
			0x41, 0xC0, 0x80, 0x42,
			});

		ASSERT_TRUE(result.has_value());
		ExpectBytesEqual(*result, expected);
	}

	TEST(NbtWriterTests, EncodesExactNetworkCompound) {
		NbtCompound compound;
		compound.Set(NbtString{ u"x" }, NbtValue::Int(42));

		const auto result = NbtWriter::WriteNetworkCompound(compound);
		const auto expected = MakeBytes({
			0x0A,
			0x03,
			0x00, 0x01, 0x78,
			0x00, 0x00, 0x00, 0x2A,
			0x00,
			});

		ASSERT_TRUE(result.has_value());
		ExpectBytesEqual(*result, expected);
	}


	TEST(NbtWriterTests, EncodesExactBigEndianScalarPayloads) {
		NbtCompound compound;
		compound.Set(NbtString{ u"s" }, NbtValue::Short(static_cast<std::int16_t>(0x1234)));
		compound.Set(NbtString{ u"i" }, NbtValue::Int(static_cast<std::int32_t>(0x12345678)));
		compound.Set(NbtString{ u"l" }, NbtValue::Long(static_cast<std::int64_t>(0x0123456789ABCDEFull)));
		compound.Set(NbtString{ u"f" }, NbtValue::Float(std::bit_cast<float>(0x3F800001u)));
		compound.Set(NbtString{ u"d" }, NbtValue::Double(std::bit_cast<double>(0x3FF0000000000001ull)));

		const auto result = NbtWriter::WriteNetworkCompound(compound);
		const auto expected = MakeBytes({
			0x0A,
			0x02, 0x00, 0x01, 0x73, 0x12, 0x34,
			0x03, 0x00, 0x01, 0x69, 0x12, 0x34, 0x56, 0x78,
			0x04, 0x00, 0x01, 0x6C, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
			0x05, 0x00, 0x01, 0x66, 0x3F, 0x80, 0x00, 0x01,
			0x06, 0x00, 0x01, 0x64, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
			0x00,
			});

		ASSERT_TRUE(result.has_value());
		ExpectBytesEqual(*result, expected);
	}

	TEST(NbtWriterTests, EncodesArraysAndHomogeneousLists) {
		const auto list = NbtList::Create(NbtType::Short, {
			NbtValue::Short(1),
			NbtValue::Short(-2),
			});
		ASSERT_TRUE(list.has_value());

		NbtCompound compound;
		compound.Set(NbtString{ u"b" }, NbtValue::ByteArray({ -1, 1 }));
		compound.Set(NbtString{ u"l" }, NbtValue::List(*list));
		compound.Set(NbtString{ u"i" }, NbtValue::IntArray({ 1, -2 }));
		compound.Set(NbtString{ u"q" }, NbtValue::LongArray({ 1, -2 }));

		const auto result = NbtWriter::WriteNetworkCompound(compound);
		const auto expected = MakeBytes({
			0x0A,
			0x07, 0x00, 0x01, 0x62, 0x00, 0x00, 0x00, 0x02, 0xFF, 0x01,
			0x09, 0x00, 0x01, 0x6C, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xFF, 0xFE,
			0x0B, 0x00, 0x01, 0x69, 0x00, 0x00, 0x00, 0x02,
			0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFE,
			0x0C, 0x00, 0x01, 0x71, 0x00, 0x00, 0x00, 0x02,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
			0x00,
			});

		ASSERT_TRUE(result.has_value());
		ExpectBytesEqual(*result, expected);
	}

	TEST(NbtReaderTests, ReadsExactNamedByteDocument) {
		const auto input = MakeBytes({ 0x01, 0x00, 0x01, 0x78, 0xFB, 0xAA });
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->BytesConsumed, 5u);
		EXPECT_EQ(result->Value.Name.GetUtf16(), u"x");
		EXPECT_EQ(result->Value.Root.GetType(), NbtType::Byte);
		EXPECT_EQ(*result->Value.Root.AsByte(), -5);
	}

	TEST(NbtReaderTests, AcceptsNegativeListLengthAsEmpty) {
		const auto input = MakeBytes({
			0x09,
			0x00, 0x00,
			0x03,
			0xFF, 0xFF, 0xFF, 0xFF,
			});
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_TRUE(result.has_value());
		ASSERT_NE(result->Value.Root.AsList(), nullptr);
		EXPECT_TRUE(result->Value.Root.AsList()->Empty());
		EXPECT_EQ(result->Value.Root.AsList()->GetElementType(), NbtType::Int);
	}

	TEST(NbtReaderTests, RejectsPositiveEndTypedList) {
		const auto input = MakeBytes({
			0x09,
			0x00, 0x00,
			0x00,
			0x00, 0x00, 0x00, 0x01,
			});
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::InvalidListElementType);
		EXPECT_EQ(result.error().Offset, 3u);
	}

	TEST(NbtReaderTests, RejectsUnknownTypeAtExactOffset) {
		const auto input = MakeBytes({ 0x7F });
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::UnknownTagType);
		EXPECT_EQ(result.error().Offset, 0u);
	}

	TEST(NbtReaderTests, RejectsNonCompoundNetworkRoot) {
		const auto input = MakeBytes({ 0x03 });
		const auto result = NbtReader::ReadNetworkCompound(input);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::InvalidRootType);
		EXPECT_EQ(result.error().Offset, 0u);
	}

	TEST(NbtReaderTests, RejectsNegativeArrayLength) {
		const auto input = MakeBytes({
			0x07,
			0x00, 0x00,
			0xFF, 0xFF, 0xFF, 0xFF,
			});
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::NegativeLength);
		EXPECT_EQ(result.error().Offset, 3u);
	}

	TEST(NbtReaderTests, RejectsInvalidModifiedUtf8) {
		const auto input = MakeBytes({
			0x08,
			0x00, 0x00,
			0x00, 0x01, 0x00,
			});
		const auto result = NbtReader::ReadDocument(input);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::InvalidModifiedUtf8);
		EXPECT_EQ(result.error().Offset, 5u);
	}

	TEST(NbtRoundTripTests, PreservesEveryTagTypeAndFloatingPointBits) {
		const auto document = MakeNestedDocument();
		const auto encoded = NbtWriter::WriteDocument(document);
		ASSERT_TRUE(encoded.has_value());

		const auto decoded = NbtReader::ReadDocument(*encoded);
		ASSERT_TRUE(decoded.has_value());
		EXPECT_EQ(decoded->BytesConsumed, encoded->size());
		EXPECT_TRUE(decoded->Value.ExactEquals(document));
	}

	TEST(NbtRoundTripTests, PreservesNetworkCompound) {
		const auto document = MakeNestedDocument();
		const auto compound = document.Root.AsCompound();
		ASSERT_NE(compound, nullptr);

		const auto encoded = NbtWriter::WriteNetworkCompound(*compound);
		ASSERT_TRUE(encoded.has_value());
		const auto decoded = NbtReader::ReadNetworkCompound(*encoded);

		ASSERT_TRUE(decoded.has_value());
		EXPECT_EQ(decoded->BytesConsumed, encoded->size());
		EXPECT_TRUE(decoded->Value.ExactEquals(*compound));
	}

	TEST(NbtReaderTests, RejectsEveryTruncationOfValidDocument) {
		const auto document = MakeNestedDocument();
		const auto encoded = NbtWriter::WriteDocument(document);
		ASSERT_TRUE(encoded.has_value());

		for (std::size_t length{ 0 }; length < encoded->size(); ++length) {
			SCOPED_TRACE(::testing::Message() << "Length: " << length);
			const auto result = NbtReader::ReadDocument(std::span<const std::byte>{ *encoded }.first(length));
			ASSERT_FALSE(result.has_value());
			EXPECT_EQ(result.error().Code, NbtReadErrorCode::IncompleteData);
		}
	}

	TEST(NbtLimitTests, EnforcesTotalReadByteLimit) {
		const auto input = MakeBytes({ 0x01, 0x00, 0x00, 0x01 });
		NbtLimits limits;
		limits.MaximumTotalBytes = 3;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::TotalByteLimitExceeded);
	}

	TEST(NbtLimitTests, EnforcesStringReadLimit) {
		const auto input = MakeBytes({ 0x08, 0x00, 0x00, 0x00, 0x02, 0x61, 0x62 });
		NbtLimits limits;
		limits.MaximumStringBytes = 1;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::StringByteLimitExceeded);
	}

	TEST(NbtLimitTests, EnforcesArrayReadLimitBeforeAllocation) {
		const auto input = MakeBytes({ 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 });
		NbtLimits limits;
		limits.MaximumArrayElements = 1;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::ArrayElementLimitExceeded);
	}

	TEST(NbtLimitTests, EnforcesListReadLimitBeforeAllocation) {
		const auto input = MakeBytes({
			0x09, 0x00, 0x00,
			0x01, 0x00, 0x00, 0x00, 0x02,
			0x00, 0x00,
			});
		NbtLimits limits;
		limits.MaximumListElements = 1;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::ListElementLimitExceeded);
	}

	TEST(NbtLimitTests, EnforcesCompoundEntryReadLimit) {
		const auto input = MakeBytes({
			0x0A, 0x00, 0x00,
			0x01, 0x00, 0x01, 0x61, 0x00,
			0x01, 0x00, 0x01, 0x62, 0x00,
			0x00,
			});
		NbtLimits limits;
		limits.MaximumCompoundEntries = 1;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::CompoundEntryLimitExceeded);
	}

	TEST(NbtLimitTests, EnforcesReadDepthLimit) {
		const auto input = MakeBytes({
			0x0A, 0x00, 0x00,
			0x0A, 0x00, 0x01, 0x61,
			0x01, 0x00, 0x01, 0x62, 0x00,
			0x00,
			0x00,
			});
		NbtLimits limits;
		limits.MaximumDepth = 0;

		const auto result = NbtReader::ReadDocument(input, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtReadErrorCode::DepthLimitExceeded);
		EXPECT_EQ(result.error().Depth, 1u);
	}

	TEST(NbtLimitTests, EnforcesWriteLimitsBeforePublishingOutput) {
		const auto document = MakeNestedDocument();
		NbtLimits limits;
		limits.MaximumTotalBytes = 8;

		const auto result = NbtWriter::WriteDocument(document, limits);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtWriteErrorCode::TotalByteLimitExceeded);
	}


	TEST(NbtLimitTests, AcceptsMaximumModifiedUtf8EncodedLength) {
		const NbtDocument document{
			.Name = NbtString{},
			.Root = NbtValue::String(NbtString{ std::u16string(65'535, u'a') }),
		};

		const auto encoded = NbtWriter::WriteDocument(document);
		ASSERT_TRUE(encoded.has_value());
		const auto decoded = NbtReader::ReadDocument(*encoded);
		ASSERT_TRUE(decoded.has_value());
		EXPECT_TRUE(decoded->Value.ExactEquals(document));
	}

	TEST(NbtLimitTests, EnforcesModifiedUtf8MaximumEncodedLength) {
		const NbtDocument document{
			.Name = NbtString{},
			.Root = NbtValue::String(NbtString{ std::u16string(32'768, u'\0') }),
		};

		const auto result = NbtWriter::WriteDocument(document);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, NbtWriteErrorCode::StringByteLimitExceeded);
	}
}
