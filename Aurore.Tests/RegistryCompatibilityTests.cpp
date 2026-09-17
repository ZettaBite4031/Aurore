#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/ResourceLocation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Protocol::Protocol774RegistryManifest;
		using Aurore::Protocol::RegistryCompatibilityInventoryEntry;
		using Aurore::Protocol::RegistryCompatibilityManifest;
		using Aurore::Protocol::RegistryCompatibilityRule;
		using Aurore::Protocol::RegistryCompatibilityValidator;
		using Aurore::Protocol::RegistryEntryDataPolicy;
		using Aurore::Protocol::RegistryInventoryErrorCode;
		using Aurore::Protocol::RegistryManifestCompleteness;
		using Aurore::Protocol::RegistryManifestErrorCode;
		using Aurore::Protocol::RegistryPresence;
		using Aurore::Protocol::RegistryRepresentation;
		using Aurore::Protocol::RegistryTagPolicy;
		using Aurore::Protocol::RegistrySnapshotInventoryAdapter;
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::RegistrySnapshotBuilder;
		using Aurore::Util::DimensionTypeDefinition;
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtString;
		using Aurore::Util::NbtValue;

		NbtCompound MakeData(std::int32_t generation_marker) {
			NbtCompound data;
			data.Set(NbtString{ u"generation" }, NbtValue::Int(generation_marker));
			return data;
		}

		[[nodiscard]] ResourceLocation Location(
			std::string_view value) {

			return ResourceLocation::Parse(value).value();
		}

		[[nodiscard]] std::vector<
			RegistryCompatibilityInventoryEntry>
			BuildCompleteInventory(
				const RegistryCompatibilityManifest& manifest) {

			std::vector<RegistryCompatibilityInventoryEntry> result;

			for (const auto& rule : manifest.GetRules()) {
				if (rule.Presence == RegistryPresence::Optional
					&& rule.EntryData
					== RegistryEntryDataPolicy::Forbidden) {
					continue;
				}

				const auto entry_count =
					std::max<std::size_t>(
						rule.MinimumEntries,
						rule.EntryData
						== RegistryEntryDataPolicy::Forbidden
						? 0
						: 1);

				result.push_back(
					RegistryCompatibilityInventoryEntry{
						.Key = rule.Key,
						.EntryCount = entry_count,
						.EntriesWithData =
							rule.EntryData
								== RegistryEntryDataPolicy::Required
								? entry_count
								: 0,
						.TagCount = rule.Tags == RegistryTagPolicy::Required ? 1ul : 0ul,
					});
			}

			return result;
		}

		[[nodiscard]] RegistryCompatibilityRule MakeRule(
			std::string_view key) {

			return RegistryCompatibilityRule{
				.Key = Location(key),
				.Presence = RegistryPresence::Required,
				.EntryData = RegistryEntryDataPolicy::Required,
				.Tags = RegistryTagPolicy::Optional,
				.Representation =
					RegistryRepresentation::GenericNetwork,
				.MinimumEntries = 1,
				.SchemaContract = "aurore_test:schema",
				.SourceCodec = "aurore_test:codec",
			};
		}
	}

	TEST(
		RegistryCompatibilityManifestTests,
		BuildsProvisionalProtocol774Manifest) {

		const auto manifest = Protocol774RegistryManifest::Build();

		ASSERT_TRUE(manifest.has_value());
		EXPECT_EQ(manifest->GetMinecraftVersion(), "1.21.11");
		EXPECT_EQ(manifest->GetProtocolVersion(), 774);
		EXPECT_EQ(
			manifest->GetCompleteness(),
			RegistryManifestCompleteness::Provisional);
		EXPECT_EQ(manifest->GetRules().size(), 13u);
	}

	TEST(
		RegistryCompatibilityManifestTests,
		ClassifiesTypedAndGenericRegistries) {

		const auto manifest = Protocol774RegistryManifest::Build();
		ASSERT_TRUE(manifest.has_value());

		const auto* dimension_type =
			manifest->Find(Location("minecraft:dimension_type"));
		const auto* cat_variant =
			manifest->Find(Location("minecraft:cat_variant"));

		ASSERT_NE(dimension_type, nullptr);
		ASSERT_NE(cat_variant, nullptr);

		EXPECT_EQ(
			dimension_type->Representation,
			RegistryRepresentation::TypedDomain);
		EXPECT_EQ(
			cat_variant->Representation,
			RegistryRepresentation::GenericNetwork);

		EXPECT_EQ(
			dimension_type->MinimumEntries,
			1u);
		EXPECT_EQ(
			cat_variant->MinimumEntries,
			1u);
	}

	TEST(
		RegistryCompatibilityManifestTests,
		RejectsDuplicateRegistryRules) {

		auto first = MakeRule("aurore_test:registry");
		auto second = first;

		auto manifest = RegistryCompatibilityManifest::Create(
			"1.21.11",
			774,
			RegistryManifestCompleteness::Provisional,
			{ std::move(first), std::move(second) });

		ASSERT_FALSE(manifest.has_value());
		EXPECT_EQ(
			manifest.error().Code,
			RegistryManifestErrorCode::DuplicateRegistry);
		EXPECT_EQ(manifest.error().RuleIndex, 1u);
		EXPECT_EQ(manifest.error().ExistingRuleIndex, 0u);
	}

	TEST(
		RegistryCompatibilityValidatorTests,
		AcceptsCompleteObservedInventory) {

		const auto manifest = Protocol774RegistryManifest::Build();
		ASSERT_TRUE(manifest.has_value());

		const auto inventory = BuildCompleteInventory(*manifest);
		const auto result =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				inventory);

		EXPECT_TRUE(result.has_value());
	}

	TEST(
		RegistryCompatibilityValidatorTests,
		RejectsMissingRequiredRegistry) {

		const auto manifest = Protocol774RegistryManifest::Build();
		ASSERT_TRUE(manifest.has_value());

		auto inventory = BuildCompleteInventory(*manifest);
		const auto missing_key =
			Location("minecraft:cat_variant");

		std::erase_if(
			inventory,
			[&missing_key](const auto& entry) {
				return entry.Key == missing_key;
			});

		const auto result =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				inventory);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			RegistryInventoryErrorCode::MissingRequiredRegistry);
		EXPECT_EQ(result.error().Key, missing_key);
	}

	TEST(
		RegistryCompatibilityValidatorTests,
		RejectsMissingRequiredEntryData) {

		const auto manifest = Protocol774RegistryManifest::Build();
		ASSERT_TRUE(manifest.has_value());

		auto inventory = BuildCompleteInventory(*manifest);
		const auto key =
			Location("minecraft:dimension_type");

		auto iterator = std::ranges::find(
			inventory,
			key,
			&RegistryCompatibilityInventoryEntry::Key);

		ASSERT_NE(iterator, inventory.end());

		iterator->EntryCount = 1;
		iterator->EntriesWithData = 0;

		const auto result =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				inventory);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			RegistryInventoryErrorCode::
			MissingRequiredEntryData);
		EXPECT_EQ(result.error().Key, key);
		EXPECT_EQ(result.error().ObservedValue, 0u);
		EXPECT_EQ(result.error().RequiredValue, 1u);
	}

	TEST(
		RegistryCompatibilityValidatorTests,
		ProvisionalManifestAllowsUnclassifiedRegistries) {

		const auto manifest = Protocol774RegistryManifest::Build();
		ASSERT_TRUE(manifest.has_value());

		auto inventory = BuildCompleteInventory(*manifest);
		inventory.push_back(
			RegistryCompatibilityInventoryEntry{
				.Key = Location("minecraft:unclassified_registry"),
				.EntryCount = 1,
				.EntriesWithData = 1,
				.TagCount = 0,
			});

		const auto result =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				inventory);

		EXPECT_TRUE(result.has_value());
	}

	TEST(
		RegistryCompatibilityValidatorTests,
		CompleteManifestRejectsUnclassifiedRegistries) {

		auto manifest = RegistryCompatibilityManifest::Create(
			"1.21.11",
			774,
			RegistryManifestCompleteness::Complete,
			{ MakeRule("aurore_test:expected") });

		ASSERT_TRUE(manifest.has_value());

		const std::vector inventory{
			RegistryCompatibilityInventoryEntry{
				.Key = Location("aurore_test:expected"),
				.EntryCount = 1,
				.EntriesWithData = 1,
				.TagCount = 0,
			},
			RegistryCompatibilityInventoryEntry{
				.Key = Location("aurore_test:unexpected"),
				.EntryCount = 1,
				.EntriesWithData = 1,
				.TagCount = 0,
			},
		};

		const auto result =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				inventory);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			RegistryInventoryErrorCode::UnexpectedRegistry);
		EXPECT_EQ(
			result.error().Key,
			Location("aurore_test:unexpected"));
	}

	TEST(
		RegistrySnapshotInventoryAdapterTests,
		BuildsTypedAndGenericInventoryInManifestOrder) {

		RegistrySnapshotBuilder snapshot_builder;

		/*
			Typed registry: minecraft:dimension_type
		*/
		ASSERT_TRUE(
			snapshot_builder.DimensionTypes().Declare(
				Location("minecraft:overworld"),
				DimensionTypeDefinition{
					.Data = MakeData(1),
				}).has_value());

		/*
			Generic registry: minecraft:cat_variant
		*/
		const auto cat_registry_key =
			Location("minecraft:cat_variant");

		auto cat_builder =
			snapshot_builder.NetworkRegistries()
			.CreateRegistryBuilder(cat_registry_key);

		ASSERT_TRUE(
			cat_builder.Declare(
				Location("minecraft:black"),
				MakeData(1)).has_value());

		ASSERT_TRUE(
			cat_builder.Declare(
				Location("minecraft:tabby"),
				MakeData(2)).has_value());

		auto cat_registry =
			std::move(cat_builder).Build();

		ASSERT_TRUE(cat_registry.has_value());

		ASSERT_TRUE(
			snapshot_builder.NetworkRegistries()
			.Declare(std::move(*cat_registry))
			.has_value());

		/*
			Generic tag section for minecraft:cat_variant.
		*/
		auto cat_tags =
			snapshot_builder.NetworkRegistryTags()
			.CreateSectionBuilder(cat_registry_key);

		ASSERT_TRUE(
			cat_tags.Declare(
				Location("minecraft:domestic"),
				{
					Location("minecraft:black"),
					Location("minecraft:tabby"),
				}).has_value());

		ASSERT_TRUE(
			snapshot_builder.NetworkRegistryTags()
			.Declare(std::move(cat_tags))
			.has_value());

		auto snapshot =
			std::move(snapshot_builder).Build(1);

		ASSERT_TRUE(snapshot.has_value());

		/*
			The manifest order is intentionally dimension_type followed
			by cat_variant. The resulting inventory must preserve this
			order regardless of the snapshot's internal representation.
		*/
		auto dimension_rule =
			MakeRule("minecraft:dimension_type");

		dimension_rule.Presence =
			RegistryPresence::Required;

		dimension_rule.EntryData =
			RegistryEntryDataPolicy::Required;

		dimension_rule.Tags =
			RegistryTagPolicy::Optional;

		dimension_rule.Representation =
			RegistryRepresentation::TypedDomain;

		dimension_rule.MinimumEntries = 1;

		auto cat_rule =
			MakeRule("minecraft:cat_variant");

		cat_rule.Presence =
			RegistryPresence::Required;

		cat_rule.EntryData =
			RegistryEntryDataPolicy::Required;

		cat_rule.Tags =
			RegistryTagPolicy::Required;

		cat_rule.Representation =
			RegistryRepresentation::GenericNetwork;

		cat_rule.MinimumEntries = 1;

		auto manifest =
			RegistryCompatibilityManifest::Create(
				"1.21.11",
				774,
				RegistryManifestCompleteness::Provisional,
				{
					std::move(dimension_rule),
					std::move(cat_rule),
				});

		ASSERT_TRUE(manifest.has_value());

		const auto inventory =
			RegistrySnapshotInventoryAdapter::Build(
				*manifest,
				**snapshot);

		ASSERT_TRUE(inventory.has_value());
		ASSERT_EQ(inventory->size(), 2u);

		/*
			Typed dimension-type registry.
		*/
		const auto& dimension_inventory =
			(*inventory)[0];

		EXPECT_EQ(
			dimension_inventory.Key,
			Location("minecraft:dimension_type"));

		EXPECT_EQ(
			dimension_inventory.EntryCount,
			1u);

		EXPECT_EQ(
			dimension_inventory.EntriesWithData,
			1u);

		EXPECT_EQ(
			dimension_inventory.TagCount,
			0u);

		/*
			Generic cat-variant registry.
		*/
		const auto& cat_inventory =
			(*inventory)[1];

		EXPECT_EQ(
			cat_inventory.Key,
			Location("minecraft:cat_variant"));

		EXPECT_EQ(
			cat_inventory.EntryCount,
			2u);

		EXPECT_EQ(
			cat_inventory.EntriesWithData,
			2u);

		EXPECT_EQ(
			cat_inventory.TagCount,
			1u);

		const auto validation =
			RegistryCompatibilityValidator::Validate(
				*manifest,
				*inventory);

		ASSERT_TRUE(validation.has_value())
			<< "Registry validation failed with code "
			<< static_cast<int>(
				validation.error().Code);
	}
}
