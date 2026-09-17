#include <Aurore/Util/ResourceLocation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::ResourceLocationErrorCode;

		constexpr bool IsExpectedNamespaceByte(std::uint16_t value) noexcept {
			return (value >= static_cast<std::uint16_t>('a') && value <= static_cast<std::uint16_t>('z'))
				|| (value >= static_cast<std::uint16_t>('0') && value <= static_cast<std::uint16_t>('9'))
				|| value == static_cast<std::uint16_t>('_')
				|| value == static_cast<std::uint16_t>('-')
				|| value == static_cast<std::uint16_t>('.');
		}

		constexpr bool IsExpectedPathByte(std::uint16_t value) noexcept {
			return IsExpectedNamespaceByte(value) || value == static_cast<std::uint16_t>('/');
		}

		void ExpectParseError(std::string_view value, ResourceLocationErrorCode code, std::size_t offset) {
			const auto result = ResourceLocation::Parse(value);

			ASSERT_FALSE(result.has_value());
			EXPECT_EQ(result.error().Code, code);
			EXPECT_EQ(result.error().Offset, offset);
		}

		void ExpectPartsError(
			std::string_view namespace_name,
			std::string_view path,
			ResourceLocationErrorCode code,
			std::size_t offset) {

			const auto result = ResourceLocation::FromParts(namespace_name, path);

			ASSERT_FALSE(result.has_value());
			EXPECT_EQ(result.error().Code, code);
			EXPECT_EQ(result.error().Offset, offset);
		}
	}

	TEST(ResourceLocationTests, ParsesExplicitNamespaceAndPath) {
		const auto result = ResourceLocation::Parse("aurore_test:dimension_type/primary");

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetNamespace(), "aurore_test");
		EXPECT_EQ(result->GetPath(), "dimension_type/primary");
		EXPECT_EQ(result->ToString(), "aurore_test:dimension_type/primary");
	}

	TEST(ResourceLocationTests, AppliesDefaultNamespaceToUnqualifiedPath) {
		const auto result = ResourceLocation::Parse("stone");

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetNamespace(), ResourceLocation::DefaultNamespace);
		EXPECT_EQ(result->GetPath(), "stone");
		EXPECT_EQ(result->ToString(), "minecraft:stone");
	}

	TEST(ResourceLocationTests, ConstructsFromValidatedParts) {
		const auto result = ResourceLocation::FromParts("aurore_test", "block/polished_stone");

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetNamespace(), "aurore_test");
		EXPECT_EQ(result->GetPath(), "block/polished_stone");
		EXPECT_EQ(result->ToString(), "aurore_test:block/polished_stone");
	}

	TEST(ResourceLocationTests, AcceptsAllDocumentedPunctuation) {
		const auto result = ResourceLocation::FromParts("namespace_1.-test", "path_1.-test/nested");

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->ToString(), "namespace_1.-test:path_1.-test/nested");
	}

	TEST(ResourceLocationTests, AcceptsPathSeparatorsAtAnyPosition) {
		constexpr std::array<std::string_view, 4> Paths{
			"/",
			"/block",
			"block/",
			"block//state",
		};

		for (const auto path : Paths) {
			SCOPED_TRACE(::testing::Message() << "Path: " << path);

			const auto result = ResourceLocation::FromParts("minecraft", path);
			ASSERT_TRUE(result.has_value());
			EXPECT_EQ(result->GetPath(), path);
		}
	}

	TEST(ResourceLocationTests, DefaultAndExplicitNamespaceFormsAreEqual) {
		const auto default_form = ResourceLocation::Parse("stone");
		const auto explicit_form = ResourceLocation::Parse("minecraft:stone");

		ASSERT_TRUE(default_form.has_value());
		ASSERT_TRUE(explicit_form.has_value());
		EXPECT_EQ(*default_form, *explicit_form);
	}

	TEST(ResourceLocationTests, RejectsEmptyInput) {
		ExpectParseError("", ResourceLocationErrorCode::EmptyValue, 0);
	}

	TEST(ResourceLocationTests, RejectsEmptyNamespace) {
		ExpectParseError(":stone", ResourceLocationErrorCode::EmptyNamespace, 0);
		ExpectPartsError("", "stone", ResourceLocationErrorCode::EmptyNamespace, 0);
	}

	TEST(ResourceLocationTests, RejectsEmptyPath) {
		ExpectParseError("minecraft:", ResourceLocationErrorCode::EmptyPath, 10);
		ExpectPartsError("minecraft", "", ResourceLocationErrorCode::EmptyPath, 0);
	}

	TEST(ResourceLocationTests, RejectsMultipleSeparatorsAtExactOffset) {
		ExpectParseError("minecraft:stone:extra", ResourceLocationErrorCode::MultipleSeparators, 15);
		ExpectParseError("minecraft::stone", ResourceLocationErrorCode::MultipleSeparators, 10);
	}

	TEST(ResourceLocationTests, RejectsUppercaseCharactersAtExactOffsets) {
		ExpectParseError("Minecraft:stone", ResourceLocationErrorCode::InvalidNamespaceCharacter, 0);
		ExpectParseError("minecraft:Stone", ResourceLocationErrorCode::InvalidPathCharacter, 10);
	}

	TEST(ResourceLocationTests, RejectsWhitespaceAtExactOffsets) {
		ExpectParseError("mine craft:stone", ResourceLocationErrorCode::InvalidNamespaceCharacter, 4);
		ExpectParseError("minecraft:stone block", ResourceLocationErrorCode::InvalidPathCharacter, 15);
		ExpectParseError("stone block", ResourceLocationErrorCode::InvalidPathCharacter, 5);
	}

	TEST(ResourceLocationTests, RejectsCharactersReservedFromComponents) {
		ExpectPartsError("mine/craft", "stone", ResourceLocationErrorCode::InvalidNamespaceCharacter, 4);
		ExpectPartsError("minecraft", "block:stone", ResourceLocationErrorCode::InvalidPathCharacter, 5);
		ExpectParseError("minecraft:block\\stone", ResourceLocationErrorCode::InvalidPathCharacter, 15);
	}

	TEST(ResourceLocationTests, RejectsEmbeddedNullAtExactOffset) {
		std::string value{ "minecraft:sto" };
		value.push_back('\0');
		value.append("ne");

		ExpectParseError(value, ResourceLocationErrorCode::InvalidPathCharacter, 13);
	}

	TEST(ResourceLocationTests, NamespaceAcceptsExactlyTheDocumentedAsciiSet) {
		// Exercise every possible byte so locale, signed-char behavior, and non-ASCII input cannot broaden validation.
		for (std::uint16_t byte{ 0 }; byte <= 0xFFu; ++byte) {
			SCOPED_TRACE(::testing::Message() << "Byte value: " << byte);

			const std::string namespace_name(1, static_cast<char>(byte));
			const auto result = ResourceLocation::FromParts(namespace_name, "path");

			if (IsExpectedNamespaceByte(byte)) {
				ASSERT_TRUE(result.has_value());
				EXPECT_EQ(result->GetNamespace(), std::string_view{ namespace_name });
			}
			else {
				ASSERT_FALSE(result.has_value());
				EXPECT_EQ(result.error().Code, ResourceLocationErrorCode::InvalidNamespaceCharacter);
				EXPECT_EQ(result.error().Offset, 0);
			}
		}
	}

	TEST(ResourceLocationTests, PathAcceptsExactlyTheDocumentedAsciiSet) {
		// Exercise every possible byte so locale, signed-char behavior, and non-ASCII input cannot broaden validation.
		for (std::uint16_t byte{ 0 }; byte <= 0xFFu; ++byte) {
			SCOPED_TRACE(::testing::Message() << "Byte value: " << byte);

			const std::string path(1, static_cast<char>(byte));
			const auto result = ResourceLocation::FromParts("minecraft", path);

			if (IsExpectedPathByte(byte)) {
				ASSERT_TRUE(result.has_value());
				EXPECT_EQ(result->GetPath(), std::string_view{ path });
			}
			else {
				ASSERT_FALSE(result.has_value());
				EXPECT_EQ(result.error().Code, ResourceLocationErrorCode::InvalidPathCharacter);
				EXPECT_EQ(result.error().Offset, 0);
			}
		}
	}

	TEST(ResourceLocationTests, OrdersByNamespaceThenPath) {
		constexpr std::array<std::string_view, 5> Inputs{
			"zeta:item",
			"minecraft:stone",
			"alpha:z",
			"alpha:a",
			"minecraft:apple",
		};
		const std::array<std::string, 5> Expected{
			"alpha:a",
			"alpha:z",
			"minecraft:apple",
			"minecraft:stone",
			"zeta:item",
		};

		std::vector<ResourceLocation> locations;
		locations.reserve(Inputs.size());

		for (const auto input : Inputs) {
			const auto result = ResourceLocation::Parse(input);
			ASSERT_TRUE(result.has_value());
			locations.push_back(*result);
		}

		std::sort(locations.begin(), locations.end());

		std::array<std::string, 5> actual;
		std::transform(locations.begin(), locations.end(), actual.begin(), [](const ResourceLocation& location) {
			return location.ToString();
			});

		EXPECT_EQ(actual, Expected);
	}

	TEST(ResourceLocationTests, HashesEqualValuesEqually) {
		const auto default_form = ResourceLocation::Parse("stone");
		const auto explicit_form = ResourceLocation::Parse("minecraft:stone");

		ASSERT_TRUE(default_form.has_value());
		ASSERT_TRUE(explicit_form.has_value());

		const std::hash<ResourceLocation> hasher;
		EXPECT_EQ(hasher(*default_form), hasher(*explicit_form));
	}

	TEST(ResourceLocationTests, SupportsLookupInUnorderedContainers) {
		const auto explicit_key = ResourceLocation::Parse("minecraft:stone");
		const auto default_key = ResourceLocation::Parse("stone");
		const auto different_key = ResourceLocation::Parse("minecraft:dirt");

		ASSERT_TRUE(explicit_key.has_value());
		ASSERT_TRUE(default_key.has_value());
		ASSERT_TRUE(different_key.has_value());

		std::unordered_map<ResourceLocation, int> values;
		values.emplace(*explicit_key, 42);

		const auto matching = values.find(*default_key);
		ASSERT_NE(matching, values.end());
		EXPECT_EQ(matching->second, 42);
		EXPECT_EQ(values.find(*different_key), values.end());
	}
}
