#include <Aurore/Util/Registry.hpp>

#include <gtest/gtest.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Util::Registry;
		using Aurore::Util::RegistryBuildError;
		using Aurore::Util::RegistryBuildErrorCode;
		using Aurore::Util::RegistryBuilder;
		using Aurore::Util::RegistryDeclaration;
		using Aurore::Util::RegistryDeclarationErrorCode;
		using Aurore::Util::RegistryDeclarationView;
		using Aurore::Util::RegistryEntry;
		using Aurore::Util::RegistryLimits;
		using Aurore::Util::RegistryRuntimeId;
		using Aurore::Util::ResourceLocation;

		struct TestDefinition final {
			std::int32_t Value;
			std::optional<ResourceLocation> Dependency{};

			auto operator<=>(const TestDefinition&) const noexcept = default;
		};

		enum class TestSourceClass : std::uint8_t {
			Original,
			Generated,
		};

		struct TestMetadata final {
			TestSourceClass SourceClass;
			std::string Description;

			auto operator<=>(const TestMetadata&) const noexcept = default;
		};

		struct MoveOnlyDefinition final {
			explicit MoveOnlyDefinition(std::int32_t value)
				: Value(std::make_unique<std::int32_t>(value)) {}

			MoveOnlyDefinition(const MoveOnlyDefinition&) = delete;
			MoveOnlyDefinition& operator=(const MoveOnlyDefinition&) = delete;
			MoveOnlyDefinition(MoveOnlyDefinition&&) noexcept = default;
			MoveOnlyDefinition& operator=(MoveOnlyDefinition&&) noexcept = default;

			std::unique_ptr<std::int32_t> Value;
		};

		ResourceLocation Location(std::string_view value) {
			return ResourceLocation::Parse(value).value();
		}

		std::expected<void, RegistryBuildError> ValidateDependencies(
			const RegistryDeclarationView<TestDefinition>& declarations) {

			for (std::size_t index{ 0 }; index < declarations.Size(); ++index) {
				const auto& declaration = declarations.GetDeclarations()[index];
				if (!declaration.Value.Dependency.has_value()) continue;
				if (declarations.Contains(*declaration.Value.Dependency)) continue;

				return std::unexpected(RegistryBuildError{
					.Code = RegistryBuildErrorCode::MissingReference,
					.Key = declaration.Key,
					.DeclarationIndex = index,
					.ReferencedKey = declaration.Value.Dependency,
					});
			}

			return {};
		}

		using TestRegistry = Registry<TestDefinition>;
		using TestEntry = RegistryEntry<TestDefinition>;
		using TestDeclaration = RegistryDeclaration<TestDefinition>;

		static_assert(std::is_same_v<
			decltype(std::declval<const TestRegistry&>().GetEntries()),
			std::span<const TestEntry>>);
		static_assert(std::is_same_v<
			decltype(std::declval<const RegistryDeclarationView<TestDefinition>&>().GetDeclarations()),
			std::span<const TestDeclaration>>);
		static_assert(std::is_const_v<std::remove_reference_t<
			decltype(std::declval<const TestRegistry&>().GetEntries()[0])>>);
	}

	TEST(RegistryBuilderTests, BuildsEmptyRegistry) {
		RegistryBuilder<TestDefinition> builder;
		const auto result = std::move(builder).Build();

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(result->Empty());
		EXPECT_EQ(result->Size(), 0u);
		EXPECT_TRUE(result->GetEntries().empty());
	}

	TEST(RegistryBuilderTests, AssignsRuntimeIdsInDeclarationOrder) {
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(Location("aurore_test:zeta"), TestDefinition{ .Value = 30 }).has_value());
		ASSERT_TRUE(builder.Declare(Location("aurore_test:alpha"), TestDefinition{ .Value = 10 }).has_value());
		ASSERT_TRUE(builder.Declare(Location("aurore_test:middle"), TestDefinition{ .Value = 20 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 3u);

		EXPECT_EQ(result->GetEntries()[0].Key, Location("aurore_test:zeta"));
		EXPECT_EQ(result->GetEntries()[0].RuntimeId, 0u);
		EXPECT_EQ(result->GetEntries()[1].Key, Location("aurore_test:alpha"));
		EXPECT_EQ(result->GetEntries()[1].RuntimeId, 1u);
		EXPECT_EQ(result->GetEntries()[2].Key, Location("aurore_test:middle"));
		EXPECT_EQ(result->GetEntries()[2].RuntimeId, 2u);
	}

	TEST(RegistryBuilderTests, RejectsDuplicateKeysWithoutMutatingDeclarations) {
		const auto key = Location("aurore_test:block");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(key, TestDefinition{ .Value = 1 }).has_value());

		const auto duplicate = builder.Declare(key, TestDefinition{ .Value = 2 });

		ASSERT_FALSE(duplicate.has_value());
		EXPECT_EQ(duplicate.error().Code, RegistryDeclarationErrorCode::DuplicateKey);
		EXPECT_EQ(duplicate.error().Key, key);
		EXPECT_EQ(duplicate.error().DeclarationIndex, 1u);
		ASSERT_TRUE(duplicate.error().ExistingIndex.has_value());
		EXPECT_EQ(*duplicate.error().ExistingIndex, 0u);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_TRUE(builder.Contains(key));

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 1u);
		EXPECT_EQ(result->GetEntries()[0].Value.Value, 1);
	}

	TEST(RegistryBuilderTests, EnforcesEntryLimitWithoutMutatingDeclarations) {
		RegistryBuilder<TestDefinition> builder{ RegistryLimits{.MaximumEntries = 1 } };
		ASSERT_TRUE(builder.Declare(Location("aurore_test:first"), TestDefinition{ .Value = 1 }).has_value());

		const auto rejected = builder.Declare(Location("aurore_test:second"), TestDefinition{ .Value = 2 });

		ASSERT_FALSE(rejected.has_value());
		EXPECT_EQ(rejected.error().Code, RegistryDeclarationErrorCode::EntryLimitExceeded);
		EXPECT_EQ(rejected.error().DeclarationIndex, 1u);
		EXPECT_FALSE(rejected.error().ExistingIndex.has_value());
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_FALSE(builder.Contains(Location("aurore_test:second")));
	}

	TEST(RegistryBuilderTests, ReportsConfiguredLimits) {
		const RegistryLimits limits{ .MaximumEntries = 37 };
		const RegistryBuilder<TestDefinition> builder{ limits };

		EXPECT_EQ(builder.GetLimits().MaximumEntries, 37u);
		EXPECT_TRUE(builder.Empty());
	}

	TEST(RegistryLookupTests, LooksUpEntriesByKeyAndRuntimeId) {
		const auto first_key = Location("aurore_test:first");
		const auto second_key = Location("aurore_test:second");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(first_key, TestDefinition{ .Value = 11 }).has_value());
		ASSERT_TRUE(builder.Declare(second_key, TestDefinition{ .Value = 22 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());

		const auto first_by_key = result->Find(first_key);
		const auto second_by_id = result->Find(static_cast<RegistryRuntimeId>(1));
		ASSERT_NE(first_by_key, nullptr);
		ASSERT_NE(second_by_id, nullptr);
		EXPECT_EQ(first_by_key->RuntimeId, 0u);
		EXPECT_EQ(first_by_key->Value.Value, 11);
		EXPECT_EQ(second_by_id->Key, second_key);
		EXPECT_EQ(second_by_id->Value.Value, 22);
	}

	TEST(RegistryLookupTests, ProvidesRuntimeIdAndKeyReverseLookup) {
		const auto key = Location("aurore_test:block");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(key, TestDefinition{ .Value = 42 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());

		const auto runtime_id = result->FindRuntimeId(key);
		ASSERT_TRUE(runtime_id.has_value());
		EXPECT_EQ(*runtime_id, 0u);

		const auto reverse_key = result->FindKey(*runtime_id);
		ASSERT_NE(reverse_key, nullptr);
		EXPECT_EQ(*reverse_key, key);
	}

	TEST(RegistryLookupTests, MissingLookupsDoNotMutateRegistry) {
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(Location("aurore_test:block"), TestDefinition{ .Value = 42 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		const auto original_size = result->Size();

		EXPECT_EQ(result->Find(Location("aurore_test:missing")), nullptr);
		EXPECT_EQ(result->Find(static_cast<RegistryRuntimeId>(99)), nullptr);
		EXPECT_FALSE(result->FindRuntimeId(Location("aurore_test:missing")).has_value());
		EXPECT_EQ(result->FindKey(static_cast<RegistryRuntimeId>(99)), nullptr);
		EXPECT_EQ(result->Size(), original_size);
	}

	TEST(RegistryLookupTests, AllowsDuplicateValuesBecauseKeysDefineIdentity) {
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(Location("aurore_test:first"), TestDefinition{ .Value = 7 }).has_value());
		ASSERT_TRUE(builder.Declare(Location("aurore_test:second"), TestDefinition{ .Value = 7 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 2u);
		EXPECT_EQ(result->GetEntries()[0].Value.Value, 7);
		EXPECT_EQ(result->GetEntries()[1].Value.Value, 7);
	}

	TEST(RegistryValidationTests, ValidatorCanInspectDeclarationsAndKeyIndexes) {
		const auto first_key = Location("aurore_test:first");
		const auto second_key = Location("aurore_test:second");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(first_key, TestDefinition{ .Value = 1 }).has_value());
		ASSERT_TRUE(builder.Declare(second_key, TestDefinition{ .Value = 2 }).has_value());

		bool validator_called{ false };
		const auto result = std::move(builder).Build(
			[&](const RegistryDeclarationView<TestDefinition>& declarations) -> std::expected<void, RegistryBuildError> {
				validator_called = true;
				EXPECT_EQ(declarations.Size(), 2u);
				EXPECT_EQ(declarations.GetDeclarations()[0].Key, first_key);
				EXPECT_EQ(declarations.GetDeclarations()[1].Key, second_key);
				EXPECT_EQ(declarations.Find(first_key), &declarations.GetDeclarations()[0]);
				EXPECT_EQ(declarations.FindIndex(second_key), 1u);
				EXPECT_TRUE(declarations.Contains(first_key));
				return {};
			});

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(validator_called);
	}

	TEST(RegistryValidationTests, AcceptsValidSameRegistryReferences) {
		const auto parent_key = Location("aurore_test:parent");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(parent_key, TestDefinition{ .Value = 1 }).has_value());
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:child"),
			TestDefinition{ .Value = 2, .Dependency = parent_key }).has_value());

		const auto result = std::move(builder).Build(ValidateDependencies);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->Size(), 2u);
	}

	TEST(RegistryValidationTests, RejectsMissingReferencesWithoutFinalizingBuilder) {
		const auto child_key = Location("aurore_test:child");
		const auto missing_key = Location("aurore_test:missing");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(
			child_key,
			TestDefinition{ .Value = 2, .Dependency = missing_key }).has_value());

		const auto result = std::move(builder).Build(ValidateDependencies);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistryBuildErrorCode::MissingReference);
		EXPECT_EQ(result.error().Key, child_key);
		EXPECT_EQ(result.error().DeclarationIndex, 0u);
		ASSERT_TRUE(result.error().ReferencedKey.has_value());
		EXPECT_EQ(*result.error().ReferencedKey, missing_key);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_TRUE(builder.Contains(child_key));
	}

	TEST(RegistryValidationTests, PreservesValidatorProvidedStructuredErrors) {
		const auto key = Location("aurore_test:block");
		RegistryBuilder<TestDefinition> builder;
		ASSERT_TRUE(builder.Declare(key, TestDefinition{ .Value = -1 }).has_value());

		const auto result = std::move(builder).Build(
			[](const RegistryDeclarationView<TestDefinition>& declarations) -> std::expected<void, RegistryBuildError> {
				const auto& declaration = declarations.GetDeclarations()[0];
				return std::unexpected(RegistryBuildError{
					.Code = RegistryBuildErrorCode::InvalidDependency,
					.Key = declaration.Key,
					.DeclarationIndex = 0,
					.ReferencedKey = std::nullopt,
					});
			});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistryBuildErrorCode::InvalidDependency);
		EXPECT_EQ(result.error().Key, key);
		EXPECT_FALSE(result.error().ReferencedKey.has_value());
	}

	TEST(RegistryDeterminismTests, IdenticalDeclarationsProduceIdenticalRuntimeIds) {
		const std::vector<ResourceLocation> keys{
			Location("aurore_test:zeta"),
			Location("aurore_test:alpha"),
			Location("aurore_test:middle"),
		};

		RegistryBuilder<TestDefinition> first_builder;
		RegistryBuilder<TestDefinition> second_builder;
		for (std::size_t index{ 0 }; index < keys.size(); ++index) {
			ASSERT_TRUE(first_builder.Declare(keys[index], TestDefinition{ .Value = static_cast<std::int32_t>(index) }).has_value());
			ASSERT_TRUE(second_builder.Declare(keys[index], TestDefinition{ .Value = static_cast<std::int32_t>(index) }).has_value());
		}

		const auto first = std::move(first_builder).Build();
		const auto second = std::move(second_builder).Build();
		ASSERT_TRUE(first.has_value());
		ASSERT_TRUE(second.has_value());
		ASSERT_EQ(first->Size(), second->Size());

		for (std::size_t index{ 0 }; index < keys.size(); ++index) {
			EXPECT_EQ(first->GetEntries()[index].Key, second->GetEntries()[index].Key);
			EXPECT_EQ(first->GetEntries()[index].RuntimeId, second->GetEntries()[index].RuntimeId);
			EXPECT_EQ(first->FindRuntimeId(keys[index]), second->FindRuntimeId(keys[index]));
		}
	}

	TEST(RegistryMetadataTests, PreservesEntryMetadataThroughFinalization) {
		using MetadataBuilder = RegistryBuilder<TestDefinition, TestMetadata>;
		MetadataBuilder builder;
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:block"),
			TestDefinition{ .Value = 42 },
			TestMetadata{
				.SourceClass = TestSourceClass::Original,
				.Description = "Aurore-owned synthetic fixture",
			}).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Size(), 1u);
		EXPECT_EQ(result->GetEntries()[0].Metadata.SourceClass, TestSourceClass::Original);
		EXPECT_EQ(result->GetEntries()[0].Metadata.Description, "Aurore-owned synthetic fixture");
	}

	TEST(RegistryPayloadTests, SupportsMoveOnlyValues) {
		RegistryBuilder<MoveOnlyDefinition> builder;
		ASSERT_TRUE(builder.Declare(
			Location("aurore_test:move_only"),
			MoveOnlyDefinition{ 73 }).has_value());

		const auto result = std::move(builder).Build();
		ASSERT_TRUE(result.has_value());
		const auto entry = result->Find(static_cast<RegistryRuntimeId>(0));
		ASSERT_NE(entry, nullptr);
		ASSERT_NE(entry->Value.Value, nullptr);
		EXPECT_EQ(*entry->Value.Value, 73);
	}
}
