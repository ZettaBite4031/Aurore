#include <Aurore/Util/Registry.hpp>
#include <Aurore/Util/RegistryTag.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Util::NoRegistryGeneration;
		using Aurore::Util::Registry;
		using Aurore::Util::RegistryBuilder;
		using Aurore::Util::RegistryGeneration;
		using Aurore::Util::RegistryRuntimeId;
		using Aurore::Util::RegistryTag;
		using Aurore::Util::RegistryTagBuildErrorCode;
		using Aurore::Util::RegistryTagBuilder;
		using Aurore::Util::RegistryTagDeclaration;
		using Aurore::Util::RegistryTagDeclarationErrorCode;
		using Aurore::Util::RegistryTagDeclarationView;
		using Aurore::Util::RegistryTagLimits;
		using Aurore::Util::RegistryTagSet;
		using Aurore::Util::ResourceLocation;

		struct TestDefinition final {
			std::int32_t Value;
		};

		struct TestMetadata final {
			std::string Source;
		};

		ResourceLocation Location(std::string_view value) {
			return ResourceLocation::Parse(value).value();
		}

		Registry<TestDefinition> MakeRegistry(
			std::initializer_list<std::string_view> keys) {

			RegistryBuilder<TestDefinition> builder;
			std::int32_t value{ 0 };
			for (const auto key : keys)
				builder.Declare(Location(key), TestDefinition{ .Value = value++ }).value();
			return std::move(builder).Build().value();
		}

		using TestTag = RegistryTag<TestDefinition>;
		using TestTagSet = RegistryTagSet<TestDefinition>;
		using TestTagBuilder = RegistryTagBuilder<TestDefinition>;

		static_assert(std::is_same_v<
			decltype(std::declval<const TestTagSet&>().GetTags()),
			std::span<const TestTag>>);
		static_assert(std::is_same_v<
			decltype(std::declval<const RegistryTagDeclarationView<TestDefinition>&>().GetDeclarations()),
			std::span<const RegistryTagDeclaration>>);
		static_assert(std::is_const_v<std::remove_reference_t<
			decltype(std::declval<const TestTagSet&>().GetTags()[0])>>);
	}

	TEST(RegistryTagBuilderTests, BuildsEmptyTagSetForValidGeneration) {
		const auto registry = MakeRegistry({});
		TestTagBuilder builder;

		const auto result = std::move(builder).Build(1, registry);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetGeneration(), 1u);
		EXPECT_TRUE(result->Empty());
		EXPECT_EQ(result->Size(), 0u);
	}

	TEST(RegistryTagBuilderTests, AllowsEmptyTags) {
		const auto registry = MakeRegistry({ "aurore_test:block" });
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(Location("aurore_test:empty"), {}).has_value());

		const auto result = std::move(builder).Build(1, registry);

		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 1u);
		EXPECT_TRUE(result->GetTags()[0].Members.empty());
	}

	TEST(RegistryTagBuilderTests, PreservesTagAndMemberDeclarationOrder) {
		const auto registry = MakeRegistry({
			"aurore_test:zeta",
			"aurore_test:alpha",
			"aurore_test:middle",
			});
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:first_tag"),
			{ Location("aurore_test:middle"), Location("aurore_test:zeta") }).has_value());
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:second_tag"),
			{ Location("aurore_test:alpha") }).has_value());

		const auto result = std::move(builder).Build(9, registry);

		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 2u);
		EXPECT_EQ(result->GetGeneration(), 9u);
		EXPECT_EQ(result->GetTags()[0].Key, Location("aurore_test:first_tag"));
		ASSERT_EQ(result->GetTags()[0].Members.size(), 2u);
		EXPECT_EQ(result->GetTags()[0].Members[0], static_cast<RegistryRuntimeId>(2));
		EXPECT_EQ(result->GetTags()[0].Members[1], static_cast<RegistryRuntimeId>(0));
		EXPECT_EQ(result->GetTags()[1].Key, Location("aurore_test:second_tag"));
		EXPECT_EQ(result->GetTags()[1].Members[0], static_cast<RegistryRuntimeId>(1));
	}

	TEST(RegistryTagBuilderTests, ExposesReadOnlyDeclarationView) {
		TestTagBuilder builder;
		const auto tag_key = Location("aurore_test:blocks");
		const auto member_key = Location("aurore_test:block");
		ASSERT_TRUE(builder.Declare(tag_key, { member_key }).has_value());

		const auto view = builder.GetDeclarationView();

		EXPECT_EQ(view.Size(), 1u);
		EXPECT_TRUE(view.Contains(tag_key));
		EXPECT_EQ(view.FindIndex(tag_key), 0u);
		ASSERT_NE(view.Find(tag_key), nullptr);
		EXPECT_EQ(view.Find(tag_key)->Members[0], member_key);
		EXPECT_EQ(builder.Size(), 1u);
	}

	TEST(RegistryTagBuilderTests, RejectsDuplicateTagKeysWithoutMutation) {
		const auto tag_key = Location("aurore_test:blocks");
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(tag_key, { Location("aurore_test:first") }).has_value());

		const auto duplicate = builder.Declare(
			tag_key,
			{ Location("aurore_test:second") });

		ASSERT_FALSE(duplicate.has_value());
		EXPECT_EQ(duplicate.error().Code, RegistryTagDeclarationErrorCode::DuplicateTagKey);
		EXPECT_EQ(duplicate.error().TagKey, tag_key);
		EXPECT_EQ(duplicate.error().DeclarationIndex, 1u);
		EXPECT_EQ(duplicate.error().ExistingIndex, 0u);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.TotalMemberCount(), 1u);
	}

	TEST(RegistryTagBuilderTests, RejectsDuplicateMembersWithoutMutation) {
		const auto tag_key = Location("aurore_test:blocks");
		const auto member_key = Location("aurore_test:block");
		TestTagBuilder builder;

		const auto duplicate = builder.Declare(
			tag_key,
			{ member_key, Location("aurore_test:other"), member_key });

		ASSERT_FALSE(duplicate.has_value());
		EXPECT_EQ(duplicate.error().Code, RegistryTagDeclarationErrorCode::DuplicateMember);
		EXPECT_EQ(duplicate.error().TagKey, tag_key);
		EXPECT_EQ(duplicate.error().MemberKey, member_key);
		EXPECT_EQ(duplicate.error().MemberIndex, 2u);
		EXPECT_EQ(duplicate.error().ExistingMemberIndex, 0u);
		EXPECT_TRUE(builder.Empty());
		EXPECT_EQ(builder.TotalMemberCount(), 0u);
	}

	TEST(RegistryTagLimitTests, EnforcesTagLimit) {
		RegistryTagLimits limits;
		limits.MaximumTags = 1;
		TestTagBuilder builder{ limits };
		ASSERT_TRUE(builder.Declare(Location("aurore_test:first"), {}).has_value());

		const auto rejected = builder.Declare(Location("aurore_test:second"), {});

		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error().Code, RegistryTagDeclarationErrorCode::TagLimitExceeded);
		EXPECT_EQ(builder.Size(), 1u);
	}

	TEST(RegistryTagLimitTests, EnforcesPerTagMemberLimit) {
		RegistryTagLimits limits;
		limits.MaximumMembersPerTag = 1;
		TestTagBuilder builder{ limits };

		const auto rejected = builder.Declare(
			Location("aurore_test:blocks"),
			{ Location("aurore_test:first"), Location("aurore_test:second") });

		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error().Code, RegistryTagDeclarationErrorCode::MemberLimitExceeded);
		EXPECT_TRUE(builder.Empty());
	}

	TEST(RegistryTagLimitTests, EnforcesTotalMemberLimit) {
		RegistryTagLimits limits;
		limits.MaximumMembersPerTag = 2;
		limits.MaximumTotalMembers = 2;
		TestTagBuilder builder{ limits };
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:first_tag"),
			{ Location("aurore_test:first"), Location("aurore_test:second") }).has_value());

		const auto rejected = builder.Declare(
			Location("aurore_test:second_tag"),
			{ Location("aurore_test:third") });

		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error().Code, RegistryTagDeclarationErrorCode::TotalMemberLimitExceeded);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.TotalMemberCount(), 2u);
	}

	TEST(RegistryTagValidationTests, RejectsGenerationZeroWithoutConsumingDeclarations) {
		const auto registry = MakeRegistry({ "aurore_test:block" });
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:blocks"),
			{ Location("aurore_test:block") }).has_value());

		const auto result = std::move(builder).Build(NoRegistryGeneration, registry);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistryTagBuildErrorCode::InvalidGeneration);
		EXPECT_FALSE(result.error().TagKey.has_value());
		EXPECT_EQ(builder.Size(), 1u);
	}

	TEST(RegistryTagValidationTests, RejectsMissingMemberWithoutConsumingDeclarations) {
		const auto registry = MakeRegistry({ "aurore_test:block" });
		const auto tag_key = Location("aurore_test:blocks");
		const auto missing_key = Location("aurore_test:missing");
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(tag_key, { missing_key }).has_value());

		const auto validation = builder.Validate(1, registry);
		const auto result = std::move(builder).Build(1, registry);

		ASSERT_FALSE(validation.has_value());
		EXPECT_EQ(validation.error().Code, RegistryTagBuildErrorCode::MissingMember);
		EXPECT_EQ(validation.error().TagKey, tag_key);
		EXPECT_EQ(validation.error().MemberKey, missing_key);
		EXPECT_EQ(validation.error().TagIndex, 0u);
		EXPECT_EQ(validation.error().MemberIndex, 0u);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_TRUE(builder.Contains(tag_key));
	}

	TEST(RegistryTagLookupTests, LooksUpTagsByStableKey) {
		const auto registry = MakeRegistry({ "aurore_test:block" });
		const auto tag_key = Location("aurore_test:blocks");
		TestTagBuilder builder;
		ASSERT_TRUE(builder.Declare(tag_key, { Location("aurore_test:block") }).has_value());

		const auto result = std::move(builder).Build(1, registry);

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(result->Contains(tag_key));
		ASSERT_NE(result->Find(tag_key), nullptr);
		EXPECT_EQ(result->Find(tag_key)->Members[0], static_cast<RegistryRuntimeId>(0));
		EXPECT_FALSE(result->Contains(Location("aurore_test:missing")));
		EXPECT_EQ(result->Find(Location("aurore_test:missing")), nullptr);
	}

	TEST(RegistryTagDeterminismTests, IdenticalInputsProduceIdenticalResolvedOrder) {
		const auto registry = MakeRegistry({
			"aurore_test:first",
			"aurore_test:second",
			});
		TestTagBuilder first_builder;
		TestTagBuilder second_builder;
		const auto tag_key = Location("aurore_test:values");
		const std::vector members{
			Location("aurore_test:second"),
			Location("aurore_test:first"),
		};
		ASSERT_TRUE(first_builder.Declare(tag_key, members).has_value());
		ASSERT_TRUE(second_builder.Declare(tag_key, members).has_value());

		const auto first = std::move(first_builder).Build(7, registry);
		const auto second = std::move(second_builder).Build(7, registry);

		ASSERT_TRUE(first.has_value());
		ASSERT_TRUE(second.has_value());
		EXPECT_EQ(first->GetGeneration(), second->GetGeneration());
		EXPECT_EQ(first->GetTags()[0].Key, second->GetTags()[0].Key);
		EXPECT_EQ(first->GetTags()[0].Members, second->GetTags()[0].Members);
	}

	TEST(RegistryTagBuilderTests, SupportsRegistriesWithMetadata) {
		RegistryBuilder<TestDefinition, TestMetadata> registry_builder;
		const auto member_key = Location("aurore_test:block");
		ASSERT_TRUE(registry_builder.Declare(
			member_key,
			TestDefinition{ .Value = 1 },
			TestMetadata{ .Source = "original" }).has_value());
		const auto registry = std::move(registry_builder).Build();
		ASSERT_TRUE(registry.has_value());

		TestTagBuilder tag_builder;
		ASSERT_TRUE(tag_builder.Declare(
			Location("aurore_test:blocks"),
			{ member_key }).has_value());
		const auto tags = std::move(tag_builder).Build(1, *registry);

		ASSERT_TRUE(tags.has_value());
		EXPECT_EQ(tags->GetTags()[0].Members[0], static_cast<RegistryRuntimeId>(0));
	}
}
