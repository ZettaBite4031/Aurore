#include <Aurore/Core/RegistrySnapshotStore.hpp>
#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Util/NbtBinary.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Core::RegistrySnapshotStore;
		using Aurore::Core::SyntheticRegistrySnapshotErrorCode;
		using Aurore::Core::SyntheticRegistrySnapshotFactory;
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtReader;
		using Aurore::Util::NbtWriter;
		using Aurore::Util::RegistryGeneration;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::ResourceLocation;

		ResourceLocation Location(std::string_view value) {
			return ResourceLocation::Parse(value).value();
		}

		std::shared_ptr<const RegistrySnapshot> BuildSnapshot(RegistryGeneration generation) {
			return SyntheticRegistrySnapshotFactory::Build(generation).value();
		}

		template<typename TDefinition, typename TMetadata>
		void ExpectRegistryEqual(
			const Aurore::Util::Registry<TDefinition, TMetadata>& left,
			const Aurore::Util::Registry<TDefinition, TMetadata>& right) {

			ASSERT_EQ(left.Size(), right.Size());
			for (std::size_t index{ 0 }; index < left.Size(); ++index) {
				const auto& left_entry = left.GetEntries()[index];
				const auto& right_entry = right.GetEntries()[index];
				EXPECT_EQ(left_entry.Key, right_entry.Key);
				EXPECT_EQ(left_entry.RuntimeId, right_entry.RuntimeId);
				EXPECT_EQ(left_entry.Value, right_entry.Value);
				EXPECT_EQ(left_entry.Metadata, right_entry.Metadata);
			}
		}

		template<typename T>
		void ExpectTagSetEqual(
			const Aurore::Util::RegistryTagSet<T>& left,
			const Aurore::Util::RegistryTagSet<T>& right) {

			EXPECT_EQ(left.GetGeneration(), right.GetGeneration());
			ASSERT_EQ(left.Size(), right.Size());
			for (std::size_t index{ 0 }; index < left.Size(); ++index) {
				EXPECT_EQ(left.GetTags()[index].Key, right.GetTags()[index].Key);
				EXPECT_EQ(left.GetTags()[index].Members, right.GetTags()[index].Members);
			}
		}

		template<typename TDefinition, typename TMetadata>
		std::vector<std::vector<std::byte>> SerializeRegistry(
			const Aurore::Util::Registry<TDefinition, TMetadata>& registry) {

			std::vector<std::vector<std::byte>> encoded_entries;
			encoded_entries.reserve(registry.Size());
			for (const auto& entry : registry.GetEntries()) {
				auto encoded = NbtWriter::WriteNetworkCompound(entry.Value.Data);
				EXPECT_TRUE(encoded.has_value());
				if (!encoded) continue;

				auto decoded = NbtReader::ReadNetworkCompound(*encoded);
				EXPECT_TRUE(decoded.has_value());
				if (decoded) {
					EXPECT_EQ(decoded->BytesConsumed, encoded->size());
					EXPECT_EQ(decoded->Value, entry.Value.Data);
				}

				encoded_entries.push_back(std::move(*encoded));
			}
			return encoded_entries;
		}

		const NbtCompound& FindItemData(
			const RegistrySnapshot& snapshot,
			std::string_view key) {

			return snapshot.GetItems().Find(Location(key))->Value.Data;
		}
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, RejectsGenerationZero) {
		const auto result = SyntheticRegistrySnapshotFactory::Build(
			Aurore::Util::NoRegistryGeneration);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, SyntheticRegistrySnapshotErrorCode::InvalidGeneration);
		EXPECT_FALSE(result.error().Registry.has_value());
		EXPECT_FALSE(result.error().Key.has_value());
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, BuildsCanonicalRegistrySet) {
		const auto snapshot = BuildSnapshot(1);

		EXPECT_EQ(snapshot->GetGeneration(), 1u);
		EXPECT_EQ(snapshot->GetBlocks().Size(), 2u);
		EXPECT_EQ(snapshot->GetItems().Size(), 2u);
		EXPECT_EQ(snapshot->GetBiomes().Size(), 1u);
		EXPECT_EQ(snapshot->GetDimensionTypes().Size(), 1u);

		EXPECT_NE(snapshot->GetBlocks().Find(Location("aurore_test:block")), nullptr);
		EXPECT_NE(snapshot->GetBlocks().Find(Location("aurore_test:polished_block")), nullptr);
		EXPECT_NE(snapshot->GetItems().Find(Location("aurore_test:item")), nullptr);
		EXPECT_NE(snapshot->GetItems().Find(Location("aurore_test:polished_item")), nullptr);
		EXPECT_NE(snapshot->GetBiomes().Find(Location("aurore_test:biome")), nullptr);
		EXPECT_NE(snapshot->GetDimensionTypes().Find(Location("aurore_test:dimension_type")), nullptr);
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, AssignsRuntimeIdsInCanonicalOrder) {
		const auto snapshot = BuildSnapshot(1);

		EXPECT_EQ(snapshot->GetBlocks().FindRuntimeId(Location("aurore_test:block")), 0u);
		EXPECT_EQ(snapshot->GetBlocks().FindRuntimeId(Location("aurore_test:polished_block")), 1u);
		EXPECT_EQ(snapshot->GetItems().FindRuntimeId(Location("aurore_test:item")), 0u);
		EXPECT_EQ(snapshot->GetItems().FindRuntimeId(Location("aurore_test:polished_item")), 1u);
		EXPECT_EQ(snapshot->GetBiomes().FindRuntimeId(Location("aurore_test:biome")), 0u);
		EXPECT_EQ(snapshot->GetDimensionTypes().FindRuntimeId(Location("aurore_test:dimension_type")), 0u);
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, BuildsOrderedGenerationBoundTags) {
		const auto snapshot = BuildSnapshot(7);

		EXPECT_EQ(snapshot->GetBlockTags().GetGeneration(), 7u);
		EXPECT_EQ(snapshot->GetItemTags().GetGeneration(), 7u);
		EXPECT_EQ(snapshot->GetBiomeTags().GetGeneration(), 7u);
		EXPECT_EQ(snapshot->GetDimensionTypeTags().GetGeneration(), 7u);

		const auto block_tag = snapshot->GetBlockTags().Find(Location("aurore_test:blocks"));
		const auto item_tag = snapshot->GetItemTags().Find(Location("aurore_test:items"));
		const auto biome_tag = snapshot->GetBiomeTags().Find(Location("aurore_test:biomes"));
		const auto dimension_tag = snapshot->GetDimensionTypeTags().Find(Location("aurore_test:dimension_types"));

		ASSERT_NE(block_tag, nullptr);
		ASSERT_NE(item_tag, nullptr);
		ASSERT_NE(biome_tag, nullptr);
		ASSERT_NE(dimension_tag, nullptr);

		EXPECT_EQ(block_tag->Members, (std::vector<Aurore::Util::RegistryRuntimeId>{ 0u, 1u }));
		EXPECT_EQ(item_tag->Members, (std::vector<Aurore::Util::RegistryRuntimeId>{ 0u, 1u }));
		EXPECT_EQ(biome_tag->Members, (std::vector<Aurore::Util::RegistryRuntimeId>{ 0u }));
		EXPECT_EQ(dimension_tag->Members, (std::vector<Aurore::Util::RegistryRuntimeId>{ 0u }));
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, PreservesExpectedNbtPayloadsAndReferences) {
		const auto snapshot = BuildSnapshot(1);

		const auto& item = FindItemData(*snapshot, "aurore_test:item");
		const auto& polished_item = FindItemData(*snapshot, "aurore_test:polished_item");

		ASSERT_NE(item.Find(u"maximum_stack_size"), nullptr);
		ASSERT_NE(item.Find(u"maximum_stack_size")->AsInt(), nullptr);
		EXPECT_EQ(*item.Find(u"maximum_stack_size")->AsInt(), 64);

		ASSERT_NE(item.Find(u"placed_block"), nullptr);
		ASSERT_NE(item.Find(u"placed_block")->AsString(), nullptr);
		EXPECT_EQ(item.Find(u"placed_block")->AsString()->ToUtf8().value(), "aurore_test:block");

		ASSERT_NE(polished_item.Find(u"placed_block"), nullptr);
		ASSERT_NE(polished_item.Find(u"placed_block")->AsString(), nullptr);
		EXPECT_EQ(polished_item.Find(u"placed_block")->AsString()->ToUtf8().value(), "aurore_test:polished_block");
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, SerializesAndRoundTripsEveryDefinition) {
		const auto snapshot = BuildSnapshot(1);

		EXPECT_EQ(SerializeRegistry(snapshot->GetBlocks()).size(), snapshot->GetBlocks().Size());
		EXPECT_EQ(SerializeRegistry(snapshot->GetItems()).size(), snapshot->GetItems().Size());
		EXPECT_EQ(SerializeRegistry(snapshot->GetBiomes()).size(), snapshot->GetBiomes().Size());
		EXPECT_EQ(SerializeRegistry(snapshot->GetDimensionTypes()).size(), snapshot->GetDimensionTypes().Size());
	}

	TEST(SyntheticRegistrySnapshotFactoryTests, IndependentBuildsAreDeterministic) {
		const auto first = BuildSnapshot(11);
		const auto second = BuildSnapshot(11);

		ExpectRegistryEqual(first->GetBlocks(), second->GetBlocks());
		ExpectRegistryEqual(first->GetItems(), second->GetItems());
		ExpectRegistryEqual(first->GetBiomes(), second->GetBiomes());
		ExpectRegistryEqual(first->GetDimensionTypes(), second->GetDimensionTypes());
		ExpectTagSetEqual(first->GetBlockTags(), second->GetBlockTags());
		ExpectTagSetEqual(first->GetItemTags(), second->GetItemTags());
		ExpectTagSetEqual(first->GetBiomeTags(), second->GetBiomeTags());
		ExpectTagSetEqual(first->GetDimensionTypeTags(), second->GetDimensionTypeTags());

		EXPECT_EQ(SerializeRegistry(first->GetBlocks()), SerializeRegistry(second->GetBlocks()));
		EXPECT_EQ(SerializeRegistry(first->GetItems()), SerializeRegistry(second->GetItems()));
		EXPECT_EQ(SerializeRegistry(first->GetBiomes()), SerializeRegistry(second->GetBiomes()));
		EXPECT_EQ(SerializeRegistry(first->GetDimensionTypes()), SerializeRegistry(second->GetDimensionTypes()));
	}

	TEST(SyntheticRegistrySnapshotPublicationTests, PublishesNewGenerationAndRetainsExistingHolders) {
		RegistrySnapshotStore store;
		const auto generation_one = BuildSnapshot(1);
		const auto generation_two = BuildSnapshot(2);

		ASSERT_TRUE(store.Publish(generation_one).has_value());
		const auto existing_holder = store.GetActiveSnapshot();
		ASSERT_TRUE(store.Publish(generation_two).has_value());

		ASSERT_NE(existing_holder, nullptr);
		EXPECT_EQ(existing_holder->GetGeneration(), 1u);
		EXPECT_EQ(existing_holder->GetBlockTags().GetGeneration(), 1u);
		EXPECT_EQ(store.GetActiveGeneration(), 2u);
		EXPECT_EQ(store.GetActiveSnapshot()->GetBlockTags().GetGeneration(), 2u);
		EXPECT_EQ(store.GetActiveSnapshot().get(), generation_two.get());
	}
}

