#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/NetworkRegistry.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Aurore::Tests {
	namespace {
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtString;
		using Aurore::Util::NbtValue;
		using Aurore::Util::NetworkRegistry;
		using Aurore::Util::NetworkRegistryBuilder;
		using Aurore::Util::NetworkRegistryCollectionBuilder;
		using Aurore::Util::NetworkRegistryCollectionBuildErrorCode;
		using Aurore::Util::NetworkRegistryCollectionDeclarationErrorCode;
		using Aurore::Util::NetworkRegistryCollectionLimits;
		using Aurore::Util::NetworkRegistryValue;
		using Aurore::Util::NoRegistryGeneration;
		using Aurore::Util::RegistryDeclarationErrorCode;
		using Aurore::Util::RegistryLimits;
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::NetworkRegistryTagCollectionBuilder;

		[[nodiscard]] ResourceLocation Location(std::string_view value) {
			return ResourceLocation::Parse(value).value();
		}

		[[nodiscard]] NbtCompound MakeData(
			std::int32_t marker) {

			NbtCompound data;
			data.Set(
				NbtString{ u"marker" },
				NbtValue::Int(marker));

			return data;
		}

		[[nodiscard]] NetworkRegistry MakeRegistry(
			std::string_view registry_key,
			std::initializer_list<
			std::pair<std::string_view, bool>> entries) {

			NetworkRegistryBuilder builder{
				Location(registry_key)
			};

			std::int32_t marker{ 0 };

			for (const auto& [entry_key, has_data] : entries) {
				NetworkRegistryValue data;

				if (has_data)
					data = MakeData(marker);

				builder.Declare(
					Location(entry_key),
					std::move(data)).value();

				++marker;
			}

			auto result = std::move(builder).Build();
			return std::move(result.value());
		}

		static_assert(
			!std::is_copy_constructible_v<NetworkRegistry>);

		static_assert(
			std::is_move_constructible_v<NetworkRegistry>);

		static_assert(std::is_same_v<
			decltype(
				std::declval<const NetworkRegistry&>()
				.GetEntries()),
			std::span<const NetworkRegistry::Entry>>);
	}

	TEST(
		NetworkRegistryBuilderTests,
		PreservesEntryOrderRuntimeIdsAndDataPresence) {

		NetworkRegistryBuilder builder{
			Location("minecraft:cat_variant")
		};

		ASSERT_TRUE(builder.Declare(
			Location("minecraft:black"),
			MakeData(1)).has_value());

		ASSERT_TRUE(builder.Declare(
			Location("minecraft:tabby")).has_value());

		EXPECT_EQ(builder.Size(), 2u);
		EXPECT_EQ(builder.GetEntriesWithDataCount(), 1u);

		auto result = std::move(builder).Build();

		ASSERT_TRUE(result.has_value());

		const auto& registry = *result;
		const auto entries = registry.GetEntries();

		ASSERT_EQ(entries.size(), 2u);

		EXPECT_EQ(entries[0].Key, Location("minecraft:black"));
		EXPECT_EQ(entries[0].RuntimeId, 0u);
		EXPECT_TRUE(entries[0].Value.has_value());

		EXPECT_EQ(entries[1].Key, Location("minecraft:tabby"));
		EXPECT_EQ(entries[1].RuntimeId, 1u);
		EXPECT_FALSE(entries[1].Value.has_value());

		EXPECT_EQ(
			registry.FindRuntimeId(
				Location("minecraft:tabby")),
			1u);

		EXPECT_EQ(
			registry.Find(0u),
			&entries[0]);

		EXPECT_EQ(
			registry.GetEntriesWithDataCount(),
			1u);
	}

	TEST(
		NetworkRegistryBuilderTests,
		RejectsDuplicateEntriesWithoutChangingCounts) {

		NetworkRegistryBuilder builder{
			Location("minecraft:cow_variant")
		};

		ASSERT_TRUE(builder.Declare(
			Location("minecraft:temperate"),
			MakeData(1)).has_value());

		const auto duplicate = builder.Declare(
			Location("minecraft:temperate"),
			MakeData(2));

		ASSERT_FALSE(duplicate.has_value());

		EXPECT_EQ(
			duplicate.error().Code,
			RegistryDeclarationErrorCode::DuplicateKey);

		EXPECT_EQ(duplicate.error().ExistingIndex, 0u);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.GetEntriesWithDataCount(), 1u);
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		PreservesRegistryOrderGenerationAndTotals) {

		NetworkRegistryCollectionBuilder builder;

		auto cats = MakeRegistry(
			"minecraft:cat_variant",
			{
				{ "minecraft:black", true },
				{ "minecraft:tabby", false },
			});

		auto cows = MakeRegistry(
			"minecraft:cow_variant",
			{
				{ "minecraft:temperate", true },
			});

		ASSERT_TRUE(
			builder.Declare(std::move(cats)).has_value());

		ASSERT_TRUE(
			builder.Declare(std::move(cows)).has_value());

		EXPECT_EQ(builder.Size(), 2u);
		EXPECT_EQ(builder.GetTotalEntryCount(), 3u);
		EXPECT_EQ(builder.GetEntriesWithDataCount(), 2u);

		auto result = std::move(builder).Build(7);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetGeneration(), 7u);
		EXPECT_EQ(result->Size(), 2u);
		EXPECT_EQ(result->GetTotalEntryCount(), 3u);
		EXPECT_EQ(result->GetEntriesWithDataCount(), 2u);

		const auto registries = result->GetRegistries();

		ASSERT_EQ(registries.size(), 2u);
		EXPECT_EQ(
			registries[0].GetKey(),
			Location("minecraft:cat_variant"));
		EXPECT_EQ(
			registries[1].GetKey(),
			Location("minecraft:cow_variant"));

		EXPECT_EQ(
			result->Find(
				Location("minecraft:cow_variant")),
			&registries[1]);
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		RejectsDuplicateRegistryWithoutConsumingCandidate) {

		NetworkRegistryCollectionBuilder builder;

		auto first = MakeRegistry(
			"minecraft:frog_variant",
			{
				{ "minecraft:temperate", true },
			});

		auto duplicate = MakeRegistry(
			"minecraft:frog_variant",
			{
				{ "minecraft:warm", true },
			});

		ASSERT_TRUE(
			builder.Declare(std::move(first)).has_value());

		const auto result =
			builder.Declare(std::move(duplicate));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			NetworkRegistryCollectionDeclarationErrorCode::
			DuplicateRegistry);

		EXPECT_EQ(
			result.error().ExistingRegistryIndex,
			0u);

		/*
			Declare accepted an rvalue reference but did not move from
			the candidate because validation failed.
		*/
		EXPECT_EQ(
			duplicate.GetKey(),
			Location("minecraft:frog_variant"));
		EXPECT_EQ(duplicate.Size(), 1u);

		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.GetTotalEntryCount(), 1u);
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		RejectsRegistryExceedingCollectionEntryLimit) {

		NetworkRegistryCollectionLimits limits;
		limits.Entries.MaximumEntries = 1;

		NetworkRegistryCollectionBuilder collection{ limits };

		NetworkRegistryBuilder registry_builder{
			Location("minecraft:wolf_variant"),
			RegistryLimits{
				.MaximumEntries = 2,
			}
		};

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:pale"),
			MakeData(1)).has_value());

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:ashen"),
			MakeData(2)).has_value());

		auto registry =
			std::move(registry_builder).Build().value();

		const auto result =
			collection.Declare(std::move(registry));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			NetworkRegistryCollectionDeclarationErrorCode::
			EntryLimitExceeded);

		EXPECT_EQ(result.error().RegistryEntryCount, 2u);
		EXPECT_EQ(result.error().Limit, 1u);

		EXPECT_EQ(registry.Size(), 2u);
		EXPECT_TRUE(collection.Empty());
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		RejectsTotalEntryLimitWithoutConsumingCandidate) {

		NetworkRegistryCollectionLimits limits;
		limits.Entries.MaximumEntries = 2;
		limits.MaximumTotalEntries = 2;

		NetworkRegistryCollectionBuilder builder{ limits };

		auto first = MakeRegistry(
			"minecraft:cat_variant",
			{
				{ "minecraft:black", true },
				{ "minecraft:tabby", true },
			});

		auto second = MakeRegistry(
			"minecraft:cow_variant",
			{
				{ "minecraft:temperate", true },
			});

		ASSERT_TRUE(
			builder.Declare(std::move(first)).has_value());

		const auto result =
			builder.Declare(std::move(second));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			NetworkRegistryCollectionDeclarationErrorCode::
			TotalEntryLimitExceeded);

		EXPECT_EQ(result.error().TotalEntryCount, 2u);
		EXPECT_EQ(result.error().RegistryEntryCount, 1u);
		EXPECT_EQ(result.error().Limit, 2u);

		EXPECT_EQ(second.Size(), 1u);
		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.GetTotalEntryCount(), 2u);
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		RejectsGenerationZeroWithoutConsumingRegistries) {

		NetworkRegistryCollectionBuilder builder;

		auto registry = MakeRegistry(
			"minecraft:painting_variant",
			{
				{ "minecraft:kebab", true },
			});

		ASSERT_TRUE(
			builder.Declare(std::move(registry)).has_value());

		const auto result =
			std::move(builder).Build(NoRegistryGeneration);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			NetworkRegistryCollectionBuildErrorCode::
			InvalidGeneration);

		EXPECT_EQ(builder.Size(), 1u);
		EXPECT_EQ(builder.GetTotalEntryCount(), 1u);
		EXPECT_TRUE(builder.Contains(
			Location("minecraft:painting_variant")));
	}

	TEST(
		NetworkRegistryCollectionBuilderTests,
		BuildsEmptyCollectionForValidGeneration) {

		NetworkRegistryCollectionBuilder builder;
		auto result = std::move(builder).Build(1);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->GetGeneration(), 1u);
		EXPECT_TRUE(result->Empty());
		EXPECT_EQ(result->GetTotalEntryCount(), 0u);
		EXPECT_EQ(result->GetEntriesWithDataCount(), 0u);
	}

	TEST(
		NetworkRegistryTagCollectionBuilderTests,
		ResolvesMembersAgainstNetworkRegistryRuntimeIds) {

		const auto registry_key =
			Location("minecraft:cat_variant");

		NetworkRegistryCollectionBuilder registries;

		auto registry_builder =
			registries.CreateRegistryBuilder(
				registry_key);

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:black"),
			MakeData(1)).has_value());

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:tabby"),
			MakeData(2)).has_value());

		auto registry =
			std::move(registry_builder).Build();

		ASSERT_TRUE(registry.has_value());
		ASSERT_TRUE(registries.Declare(
			std::move(*registry)).has_value());

		auto built_registries =
			std::move(registries).Build(5);

		ASSERT_TRUE(built_registries.has_value());

		NetworkRegistryTagCollectionBuilder tags;

		auto section =
			tags.CreateSectionBuilder(registry_key);

		ASSERT_TRUE(section.Declare(
			Location("minecraft:domestic"),
			{
				Location("minecraft:tabby"),
				Location("minecraft:black"),
			}).has_value());

		ASSERT_TRUE(tags.Declare(
			std::move(section)).has_value());

		auto built_tags =
			std::move(tags).Build(
				5,
				*built_registries);

		ASSERT_TRUE(built_tags.has_value());
		EXPECT_EQ(built_tags->GetGeneration(), 5u);
		EXPECT_EQ(built_tags->Size(), 1u);
		EXPECT_EQ(built_tags->GetTotalTagCount(), 1u);
		EXPECT_EQ(built_tags->GetTotalMemberCount(), 2u);

		const auto* built_section =
			built_tags->Find(registry_key);

		ASSERT_NE(built_section, nullptr);

		const auto* tag = built_section->Find(
			Location("minecraft:domestic"));

		ASSERT_NE(tag, nullptr);
		ASSERT_EQ(tag->Members.size(), 2u);

		EXPECT_EQ(tag->Members[0], 1u);
		EXPECT_EQ(tag->Members[1], 0u);
	}
}
