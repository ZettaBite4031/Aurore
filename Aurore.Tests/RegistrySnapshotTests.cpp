#include <Aurore/Core/RegistrySnapshotStore.hpp>

#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Core::RegistryPublicationErrorCode;
		using Aurore::Core::RegistrySnapshotStore;
		using Aurore::Util::BiomeDefinition;
		using Aurore::Util::BlockDefinition;
		using Aurore::Util::DimensionTypeDefinition;
		using Aurore::Util::ItemDefinition;
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtString;
		using Aurore::Util::NbtValue;
		using Aurore::Util::RegistryGeneration;
		using Aurore::Util::RegistryRuntimeId;
		using Aurore::Util::RegistryKind;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::RegistrySnapshotBuilder;
		using Aurore::Util::RegistrySnapshotDeclarationView;
		using Aurore::Util::RegistrySnapshotError;
		using Aurore::Util::RegistrySnapshotErrorCode;
		using Aurore::Util::RegistrySnapshotLimits;
		using Aurore::Util::ResourceLocation;

		ResourceLocation Location(std::string_view value) {
			return ResourceLocation::Parse(value).value();
		}

		NbtCompound MakeData(std::int32_t generation_marker) {
			NbtCompound data;
			data.Set(NbtString{ u"generation" }, NbtValue::Int(generation_marker));
			return data;
		}

		template<typename TDefinition>
		TDefinition Definition(std::int32_t generation_marker) {
			return TDefinition{ .Data = MakeData(generation_marker) };
		}

		template<typename TDefinition>
		std::optional<std::int32_t> ReadMarker(const TDefinition& definition) {
			const auto value = definition.Data.Find(u"generation");
			if (value == nullptr || value->AsInt() == nullptr) return std::nullopt;
			return *value->AsInt();
		}

		void DeclareCompleteFixture(
			RegistrySnapshotBuilder& builder,
			std::int32_t generation_marker,
			bool reverse_blocks = false) {

			if (reverse_blocks) {
				builder.Blocks().Declare(
					Location("aurore_test:polished_block"),
					Definition<BlockDefinition>(generation_marker)).value();
				builder.Blocks().Declare(
					Location("aurore_test:block"),
					Definition<BlockDefinition>(generation_marker)).value();
			}
			else {
				builder.Blocks().Declare(
					Location("aurore_test:block"),
					Definition<BlockDefinition>(generation_marker)).value();
				builder.Blocks().Declare(
					Location("aurore_test:polished_block"),
					Definition<BlockDefinition>(generation_marker)).value();
			}

			builder.Items().Declare(
				Location("aurore_test:item"),
				Definition<ItemDefinition>(generation_marker)).value();
			builder.Biomes().Declare(
				Location("aurore_test:biome"),
				Definition<BiomeDefinition>(generation_marker)).value();
			builder.DimensionTypes().Declare(
				Location("aurore_test:dimension_type"),
				Definition<DimensionTypeDefinition>(generation_marker)).value();

			builder.BlockTags().Declare(
				Location("aurore_test:blocks"),
				{ Location("aurore_test:block"), Location("aurore_test:polished_block") }).value();
			builder.ItemTags().Declare(
				Location("aurore_test:items"),
				{ Location("aurore_test:item") }).value();
			builder.BiomeTags().Declare(
				Location("aurore_test:biomes"),
				{ Location("aurore_test:biome") }).value();
			builder.DimensionTypeTags().Declare(
				Location("aurore_test:dimension_types"),
				{ Location("aurore_test:dimension_type") }).value();
		}

		std::shared_ptr<const RegistrySnapshot> MakeSnapshot(
			RegistryGeneration generation,
			std::int32_t generation_marker,
			bool reverse_blocks = false) {

			RegistrySnapshotBuilder builder;
			DeclareCompleteFixture(builder, generation_marker, reverse_blocks);
			return std::move(builder).Build(generation).value();
		}

		bool HasCoherentMarker(const RegistrySnapshot& snapshot) {
			const auto block = snapshot.GetBlocks().Find(0);
			const auto item = snapshot.GetItems().Find(0);
			const auto biome = snapshot.GetBiomes().Find(0);
			const auto dimension_type = snapshot.GetDimensionTypes().Find(0);
			if (block == nullptr || item == nullptr || biome == nullptr || dimension_type == nullptr) return false;

			const auto expected = static_cast<std::int32_t>(snapshot.GetGeneration());
			if (ReadMarker(block->Value) != expected
				|| ReadMarker(item->Value) != expected
				|| ReadMarker(biome->Value) != expected
				|| ReadMarker(dimension_type->Value) != expected) return false;

			if (snapshot.GetBlockTags().GetGeneration() != snapshot.GetGeneration()
				|| snapshot.GetItemTags().GetGeneration() != snapshot.GetGeneration()
				|| snapshot.GetBiomeTags().GetGeneration() != snapshot.GetGeneration()
				|| snapshot.GetDimensionTypeTags().GetGeneration() != snapshot.GetGeneration()) return false;

			const auto block_tag = snapshot.GetBlockTags().Find(Location("aurore_test:blocks"));
			const auto item_tag = snapshot.GetItemTags().Find(Location("aurore_test:items"));
			const auto biome_tag = snapshot.GetBiomeTags().Find(Location("aurore_test:biomes"));
			const auto dimension_type_tag = snapshot.GetDimensionTypeTags().Find(Location("aurore_test:dimension_types"));
			if (block_tag == nullptr || item_tag == nullptr || biome_tag == nullptr || dimension_type_tag == nullptr) return false;
			if (block_tag->Members.size() != 2 || item_tag->Members.size() != 1
				|| biome_tag->Members.size() != 1 || dimension_type_tag->Members.size() != 1) return false;

			return block_tag->Members[0] == snapshot.GetBlocks().FindRuntimeId(Location("aurore_test:block"))
				&& block_tag->Members[1] == snapshot.GetBlocks().FindRuntimeId(Location("aurore_test:polished_block"))
				&& item_tag->Members[0] == snapshot.GetItems().FindRuntimeId(Location("aurore_test:item"))
				&& biome_tag->Members[0] == snapshot.GetBiomes().FindRuntimeId(Location("aurore_test:biome"))
				&& dimension_type_tag->Members[0] == snapshot.GetDimensionTypes().FindRuntimeId(Location("aurore_test:dimension_type"));
		}

		static_assert(std::is_same_v<
			decltype(std::declval<const RegistrySnapshot&>().GetBlocks()),
			const Aurore::Util::BlockRegistry&>);
		static_assert(std::is_same_v<
			decltype(std::declval<const RegistrySnapshot&>().GetItems()),
			const Aurore::Util::ItemRegistry&>);
		static_assert(std::is_same_v<
			decltype(std::declval<const RegistrySnapshot&>().GetBlockTags()),
			const Aurore::Util::BlockTagSet&>);
		static_assert(std::is_const_v<std::remove_reference_t<
			decltype(std::declval<const RegistrySnapshot&>().GetBlocks().GetEntries()[0])>>);
	}

	TEST(RegistrySnapshotBuilderTests, RejectsGenerationZeroWithoutConsumingDeclarations) {
		RegistrySnapshotBuilder builder;
		ASSERT_TRUE(builder.Blocks().Declare(
			Location("aurore_test:block"),
			Definition<BlockDefinition>(1)).has_value());

		const auto result = std::move(builder).Build(Aurore::Util::NoRegistryGeneration);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistrySnapshotErrorCode::InvalidGeneration);
		EXPECT_FALSE(result.error().Registry.has_value());
		EXPECT_EQ(builder.Blocks().Size(), 1u);
		EXPECT_TRUE(builder.Blocks().Contains(Location("aurore_test:block")));
	}

	TEST(RegistrySnapshotBuilderTests, BuildsEmptySnapshotForValidGeneration) {
		RegistrySnapshotBuilder builder;
		const auto result = std::move(builder).Build(1);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ((*result)->GetGeneration(), 1u);
		EXPECT_TRUE((*result)->GetBlocks().Empty());
		EXPECT_TRUE((*result)->GetItems().Empty());
		EXPECT_TRUE((*result)->GetBiomes().Empty());
		EXPECT_TRUE((*result)->GetDimensionTypes().Empty());
		EXPECT_TRUE((*result)->GetBlockTags().Empty());
		EXPECT_TRUE((*result)->GetItemTags().Empty());
		EXPECT_TRUE((*result)->GetBiomeTags().Empty());
		EXPECT_TRUE((*result)->GetDimensionTypeTags().Empty());
		EXPECT_TRUE((*result)->GetNetworkRegistries().Empty());
		EXPECT_TRUE((*result)->GetNetworkRegistryTags().Empty());
		EXPECT_EQ((*result)->GetNetworkRegistries().GetGeneration(), 1u);
		EXPECT_EQ((*result)->GetNetworkRegistryTags().GetGeneration(), 1u);
	}

	TEST(RegistrySnapshotBuilderTests, BuildsAllTypedRegistriesUnderOneGeneration) {
		RegistrySnapshotBuilder builder;
		DeclareCompleteFixture(builder, 7);

		const auto result = std::move(builder).Build(7);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ((*result)->GetGeneration(), 7u);
		EXPECT_EQ((*result)->GetBlocks().Size(), 2u);
		EXPECT_EQ((*result)->GetItems().Size(), 1u);
		EXPECT_EQ((*result)->GetBiomes().Size(), 1u);
		EXPECT_EQ((*result)->GetDimensionTypes().Size(), 1u);
		EXPECT_EQ((*result)->GetBlockTags().Size(), 1u);
		EXPECT_EQ((*result)->GetItemTags().Size(), 1u);
		EXPECT_EQ((*result)->GetBiomeTags().Size(), 1u);
		EXPECT_EQ((*result)->GetDimensionTypeTags().Size(), 1u);
		EXPECT_TRUE(HasCoherentMarker(**result));
	}

	TEST(RegistrySnapshotBuilderTests, PreservesIndependentDeclarationOrderPerRegistry) {
		RegistrySnapshotBuilder builder;
		ASSERT_TRUE(builder.Blocks().Declare(
			Location("aurore_test:zeta"),
			Definition<BlockDefinition>(1)).has_value());
		ASSERT_TRUE(builder.Blocks().Declare(
			Location("aurore_test:alpha"),
			Definition<BlockDefinition>(1)).has_value());
		ASSERT_TRUE(builder.Items().Declare(
			Location("aurore_test:second"),
			Definition<ItemDefinition>(1)).has_value());
		ASSERT_TRUE(builder.Items().Declare(
			Location("aurore_test:first"),
			Definition<ItemDefinition>(1)).has_value());

		const auto result = std::move(builder).Build(1);
		ASSERT_TRUE(result.has_value());

		EXPECT_EQ((*result)->GetBlocks().GetEntries()[0].Key, Location("aurore_test:zeta"));
		EXPECT_EQ((*result)->GetBlocks().GetEntries()[0].RuntimeId, 0u);
		EXPECT_EQ((*result)->GetBlocks().GetEntries()[1].Key, Location("aurore_test:alpha"));
		EXPECT_EQ((*result)->GetBlocks().GetEntries()[1].RuntimeId, 1u);
		EXPECT_EQ((*result)->GetItems().GetEntries()[0].Key, Location("aurore_test:second"));
		EXPECT_EQ((*result)->GetItems().GetEntries()[0].RuntimeId, 0u);
		EXPECT_EQ((*result)->GetItems().GetEntries()[1].Key, Location("aurore_test:first"));
		EXPECT_EQ((*result)->GetItems().GetEntries()[1].RuntimeId, 1u);
	}

	TEST(RegistrySnapshotBuilderTests, AppliesPerRegistryLimits) {
		RegistrySnapshotLimits limits;
		limits.Blocks.MaximumEntries = 1;
		limits.Items.MaximumEntries = 2;
		limits.BlockTags.MaximumTags = 1;
		limits.BlockTags.MaximumMembersPerTag = 2;
		RegistrySnapshotBuilder builder{ limits };

		ASSERT_TRUE(builder.Blocks().Declare(
			Location("aurore_test:first_block"),
			Definition<BlockDefinition>(1)).has_value());
		const auto second_block = builder.Blocks().Declare(
			Location("aurore_test:second_block"),
			Definition<BlockDefinition>(1));
		ASSERT_FALSE(second_block.has_value());

		EXPECT_EQ(builder.Blocks().GetLimits().MaximumEntries, 1u);
		EXPECT_EQ(builder.Items().GetLimits().MaximumEntries, 2u);
		EXPECT_EQ(builder.BlockTags().GetLimits().MaximumTags, 1u);
		EXPECT_EQ(builder.BlockTags().GetLimits().MaximumMembersPerTag, 2u);
		EXPECT_EQ(builder.Blocks().Size(), 1u);
	}

	TEST(RegistrySnapshotValidationTests, ValidatorObservesEveryRegistryWithoutConsumingBuilders) {
		RegistrySnapshotBuilder builder;
		DeclareCompleteFixture(builder, 1);
		bool called{ false };

		const auto validation = builder.Validate(
			[&](const RegistrySnapshotDeclarationView& declarations) -> std::expected<void, RegistrySnapshotError> {
				called = true;
				EXPECT_EQ(declarations.GetBlocks().Size(), 2u);
				EXPECT_EQ(declarations.GetItems().Size(), 1u);
				EXPECT_EQ(declarations.GetBiomes().Size(), 1u);
				EXPECT_EQ(declarations.GetDimensionTypes().Size(), 1u);
				EXPECT_TRUE(declarations.GetBlocks().Contains(Location("aurore_test:block")));
				EXPECT_TRUE(declarations.GetItems().Contains(Location("aurore_test:item")));
				EXPECT_EQ(declarations.GetBlockTags().Size(), 1u);
				EXPECT_EQ(declarations.GetItemTags().Size(), 1u);
				EXPECT_TRUE(declarations.GetBlockTags().Contains(Location("aurore_test:blocks")));
				return {};
			});

		ASSERT_TRUE(validation.has_value());
		EXPECT_TRUE(called);
		EXPECT_EQ(builder.Blocks().Size(), 2u);
		EXPECT_EQ(builder.Items().Size(), 1u);
	}

	TEST(RegistrySnapshotValidationTests, AcceptsValidCrossRegistryReference) {
		const auto block_key = Location("aurore_test:block");
		const auto item_key = Location("aurore_test:item");
		RegistrySnapshotBuilder builder;
		DeclareCompleteFixture(builder, 1);

		const auto result = std::move(builder).Build(
			1,
			[&](const RegistrySnapshotDeclarationView& declarations) -> std::expected<void, RegistrySnapshotError> {
				if (declarations.GetBlocks().Contains(block_key)) return {};
				return std::unexpected(RegistrySnapshotError{
					.Code = RegistrySnapshotErrorCode::MissingReference,
					.Registry = RegistryKind::Item,
					.Key = item_key,
					.DeclarationIndex = 0,
					.ReferencedRegistry = RegistryKind::Block,
					.ReferencedKey = block_key,
					});
			});

		ASSERT_TRUE(result.has_value());
		EXPECT_NE((*result)->GetItems().Find(item_key), nullptr);
		EXPECT_NE((*result)->GetBlocks().Find(block_key), nullptr);
	}

	TEST(RegistrySnapshotValidationTests, RejectsMissingCrossRegistryReferenceWithoutFinalizing) {
		const auto item_key = Location("aurore_test:item");
		const auto missing_block = Location("aurore_test:missing_block");
		RegistrySnapshotBuilder builder;
		ASSERT_TRUE(builder.Items().Declare(
			item_key,
			Definition<ItemDefinition>(1)).has_value());

		const auto result = std::move(builder).Build(
			1,
			[&](const RegistrySnapshotDeclarationView& declarations) -> std::expected<void, RegistrySnapshotError> {
				if (declarations.GetBlocks().Contains(missing_block)) return {};
				return std::unexpected(RegistrySnapshotError{
					.Code = RegistrySnapshotErrorCode::MissingReference,
					.Registry = RegistryKind::Item,
					.Key = item_key,
					.DeclarationIndex = 0,
					.ReferencedRegistry = RegistryKind::Block,
					.ReferencedKey = missing_block,
					});
			});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistrySnapshotErrorCode::MissingReference);
		EXPECT_EQ(result.error().Registry, RegistryKind::Item);
		EXPECT_EQ(result.error().Key, item_key);
		EXPECT_EQ(result.error().ReferencedRegistry, RegistryKind::Block);
		EXPECT_EQ(result.error().ReferencedKey, missing_block);
		EXPECT_EQ(builder.Items().Size(), 1u);
		EXPECT_TRUE(builder.Items().Contains(item_key));
	}

	TEST(RegistrySnapshotTagValidationTests, RejectsMissingTagMemberWithoutFinalizingBuilders) {
		const auto tag_key = Location("aurore_test:blocks");
		const auto missing_key = Location("aurore_test:missing");
		RegistrySnapshotBuilder builder;
		ASSERT_TRUE(builder.Blocks().Declare(
			Location("aurore_test:block"),
			Definition<BlockDefinition>(1)).has_value());
		ASSERT_TRUE(builder.BlockTags().Declare(tag_key, { missing_key }).has_value());

		const auto result = std::move(builder).Build(1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistrySnapshotErrorCode::MissingReference);
		EXPECT_EQ(result.error().Registry, RegistryKind::Block);
		EXPECT_EQ(result.error().Key, tag_key);
		EXPECT_EQ(result.error().ReferencedRegistry, RegistryKind::Block);
		EXPECT_EQ(result.error().ReferencedKey, missing_key);
		EXPECT_EQ(builder.Blocks().Size(), 1u);
		EXPECT_EQ(builder.BlockTags().Size(), 1u);
		EXPECT_TRUE(builder.Blocks().Contains(Location("aurore_test:block")));
		EXPECT_TRUE(builder.BlockTags().Contains(tag_key));
	}

	TEST(RegistrySnapshotGenerationTests, StableKeysRemainValidAcrossChangedRuntimeIdAssignments) {
		const auto key = Location("aurore_test:block");
		const auto first = MakeSnapshot(1, 1, false);
		const auto second = MakeSnapshot(2, 2, true);

		const auto first_id = first->GetBlocks().FindRuntimeId(key);
		const auto second_id = second->GetBlocks().FindRuntimeId(key);
		ASSERT_TRUE(first_id.has_value());
		ASSERT_TRUE(second_id.has_value());
		EXPECT_EQ(*first_id, 0u);
		EXPECT_EQ(*second_id, 1u);
		EXPECT_NE(first->GetBlocks().Find(key), nullptr);
		EXPECT_NE(second->GetBlocks().Find(key), nullptr);

		const auto first_tag = first->GetBlockTags().Find(Location("aurore_test:blocks"));
		const auto second_tag = second->GetBlockTags().Find(Location("aurore_test:blocks"));
		ASSERT_NE(first_tag, nullptr);
		ASSERT_NE(second_tag, nullptr);
		EXPECT_EQ(first_tag->Members[0], static_cast<RegistryRuntimeId>(0));
		EXPECT_EQ(second_tag->Members[0], static_cast<RegistryRuntimeId>(1));
	}

	TEST(RegistrySnapshotStoreTests, StartsWithoutAnActiveGeneration) {
		RegistrySnapshotStore store;

		EXPECT_EQ(store.GetActiveSnapshot(), nullptr);
		EXPECT_EQ(store.GetActiveGeneration(), Aurore::Util::NoRegistryGeneration);
	}

	TEST(RegistrySnapshotStoreTests, RejectsNullSnapshot) {
		RegistrySnapshotStore store;
		const auto result = store.Publish(nullptr);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, RegistryPublicationErrorCode::NullSnapshot);
		EXPECT_EQ(result.error().AttemptedGeneration, Aurore::Util::NoRegistryGeneration);
		EXPECT_EQ(result.error().ActiveGeneration, Aurore::Util::NoRegistryGeneration);
	}

	TEST(RegistrySnapshotStoreTests, PublishesCompleteSnapshot) {
		RegistrySnapshotStore store;
		const auto snapshot = MakeSnapshot(1, 1);

		const auto result = store.Publish(snapshot);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(store.GetActiveGeneration(), 1u);
		EXPECT_EQ(store.GetActiveSnapshot().get(), snapshot.get());
		EXPECT_TRUE(HasCoherentMarker(*store.GetActiveSnapshot()));
	}

	TEST(RegistrySnapshotStoreTests, RejectsEqualAndOlderGenerations) {
		RegistrySnapshotStore store;
		const auto generation_two = MakeSnapshot(2, 2);
		ASSERT_TRUE(store.Publish(generation_two).has_value());

		const auto equal = store.Publish(MakeSnapshot(2, 2));
		const auto older = store.Publish(MakeSnapshot(1, 1));

		ASSERT_FALSE(equal.has_value());
		EXPECT_EQ(equal.error().Code, RegistryPublicationErrorCode::GenerationNotNewer);
		EXPECT_EQ(equal.error().AttemptedGeneration, 2u);
		EXPECT_EQ(equal.error().ActiveGeneration, 2u);
		ASSERT_FALSE(older.has_value());
		EXPECT_EQ(older.error().Code, RegistryPublicationErrorCode::GenerationNotNewer);
		EXPECT_EQ(older.error().AttemptedGeneration, 1u);
		EXPECT_EQ(older.error().ActiveGeneration, 2u);
		EXPECT_EQ(store.GetActiveSnapshot().get(), generation_two.get());
	}

	TEST(RegistrySnapshotStoreTests, ExistingHoldersRetainPreviousGeneration) {
		RegistrySnapshotStore store;
		const auto generation_one = MakeSnapshot(1, 1);
		const auto generation_two = MakeSnapshot(2, 2);
		ASSERT_TRUE(store.Publish(generation_one).has_value());

		const auto existing_holder = store.GetActiveSnapshot();
		ASSERT_TRUE(store.Publish(generation_two).has_value());

		ASSERT_NE(existing_holder, nullptr);
		EXPECT_EQ(existing_holder->GetGeneration(), 1u);
		EXPECT_TRUE(HasCoherentMarker(*existing_holder));
		EXPECT_EQ(store.GetActiveGeneration(), 2u);
		EXPECT_EQ(store.GetActiveSnapshot().get(), generation_two.get());
	}

	TEST(RegistrySnapshotStoreTests, ProtocolAndWorldConsumersAcquireSameSnapshotInstance) {
		RegistrySnapshotStore store;
		const auto snapshot = MakeSnapshot(1, 1);
		ASSERT_TRUE(store.Publish(snapshot).has_value());

		const auto protocol_snapshot = store.GetActiveSnapshot();
		const auto world_snapshot = store.GetActiveSnapshot();

		ASSERT_NE(protocol_snapshot, nullptr);
		ASSERT_NE(world_snapshot, nullptr);
		EXPECT_EQ(protocol_snapshot.get(), world_snapshot.get());
		EXPECT_EQ(protocol_snapshot->GetGeneration(), world_snapshot->GetGeneration());
	}

	TEST(RegistrySnapshotStoreTests, ConcurrentReadersObserveOnlyCompleteGenerations) {
		RegistrySnapshotStore store;
		const auto generation_one = MakeSnapshot(1, 1);
		const auto generation_two = MakeSnapshot(2, 2);
		ASSERT_TRUE(store.Publish(generation_one).has_value());

		std::atomic<bool> start{ false };
		std::atomic<bool> stop{ false };
		std::atomic<bool> invalid_observation{ false };
		std::vector<std::thread> readers;

		for (std::size_t index{ 0 }; index < 4; ++index) {
			readers.emplace_back([&] {
				while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
				while (!stop.load(std::memory_order_acquire)) {
					const auto snapshot = store.GetActiveSnapshot();
					if (snapshot == nullptr || !HasCoherentMarker(*snapshot)) {
						invalid_observation.store(true, std::memory_order_release);
						return;
					}
					const auto generation = snapshot->GetGeneration();
					if (generation != 1 && generation != 2) {
						invalid_observation.store(true, std::memory_order_release);
						return;
					}
				}
				});
		}

		start.store(true, std::memory_order_release);
		EXPECT_TRUE(store.Publish(generation_two).has_value());
		for (std::size_t index{ 0 }; index < 1'000; ++index) std::this_thread::yield();
		stop.store(true, std::memory_order_release);

		for (auto& reader : readers) reader.join();

		EXPECT_FALSE(invalid_observation.load(std::memory_order_acquire));
		EXPECT_EQ(store.GetActiveGeneration(), 2u);
		EXPECT_TRUE(HasCoherentMarker(*store.GetActiveSnapshot()));
	}

	TEST(RegistrySnapshotStoreTests, ConcurrentPublishersCannotRegressGeneration) {
		RegistrySnapshotStore store;
		ASSERT_TRUE(store.Publish(MakeSnapshot(1, 1)).has_value());
		const auto generation_two = MakeSnapshot(2, 2);
		const auto generation_three = MakeSnapshot(3, 3);

		std::atomic<bool> start{ false };
		std::atomic<bool> generation_two_published{ false };
		std::atomic<bool> generation_three_published{ false };

		std::thread second_publisher([&] {
			while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
			generation_two_published.store(
				store.Publish(generation_two).has_value(),
				std::memory_order_release);
			});
		std::thread third_publisher([&] {
			while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
			generation_three_published.store(
				store.Publish(generation_three).has_value(),
				std::memory_order_release);
			});

		start.store(true, std::memory_order_release);
		second_publisher.join();
		third_publisher.join();

		EXPECT_TRUE(generation_three_published.load(std::memory_order_acquire));
		EXPECT_EQ(store.GetActiveGeneration(), 3u);
		EXPECT_EQ(store.GetActiveSnapshot().get(), generation_three.get());
		EXPECT_TRUE(HasCoherentMarker(*store.GetActiveSnapshot()));
		(void)generation_two_published;
	}

	TEST(
		RegistrySnapshotBuilderTests,
		PublishesGenericRegistriesAndTagsUnderSnapshotGeneration) {

		const auto registry_key =
			Location("minecraft:cat_variant");

		RegistrySnapshotBuilder builder;

		auto registry_builder =
			builder.NetworkRegistries()
			.CreateRegistryBuilder(registry_key);

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:black"),
			MakeData(7)).has_value());

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:tabby"),
			MakeData(7)).has_value());

		auto registry =
			std::move(registry_builder).Build();

		ASSERT_TRUE(registry.has_value());

		ASSERT_TRUE(
			builder.NetworkRegistries().Declare(
				std::move(*registry)).has_value());

		auto tag_section =
			builder.NetworkRegistryTags()
			.CreateSectionBuilder(registry_key);

		ASSERT_TRUE(tag_section.Declare(
			Location("minecraft:domestic"),
			{
				Location("minecraft:black"),
				Location("minecraft:tabby"),
			}).has_value());

		ASSERT_TRUE(
			builder.NetworkRegistryTags().Declare(
				std::move(tag_section)).has_value());

		auto result = std::move(builder).Build(7);

		ASSERT_TRUE(result.has_value());

		const auto& snapshot = **result;

		EXPECT_EQ(snapshot.GetGeneration(), 7u);
		EXPECT_EQ(
			snapshot.GetNetworkRegistries()
			.GetGeneration(),
			7u);

		EXPECT_EQ(
			snapshot.GetNetworkRegistryTags()
			.GetGeneration(),
			7u);

		const auto* generic_registry =
			snapshot.GetNetworkRegistries().Find(
				registry_key);

		const auto* generic_tags =
			snapshot.GetNetworkRegistryTags().Find(
				registry_key);

		ASSERT_NE(generic_registry, nullptr);
		ASSERT_NE(generic_tags, nullptr);

		EXPECT_EQ(generic_registry->Size(), 2u);
		EXPECT_EQ(generic_tags->Size(), 1u);
	}

	TEST(
		RegistrySnapshotBuilderTests,
		RejectsGenericRegistryUsingTypedRegistryKey) {

		RegistrySnapshotBuilder builder;

		auto registry_builder =
			builder.NetworkRegistries()
			.CreateRegistryBuilder(
				Location(
					"minecraft:dimension_type"));

		ASSERT_TRUE(registry_builder.Declare(
			Location("minecraft:overworld"),
			MakeData(1)).has_value());

		auto registry =
			std::move(registry_builder).Build();

		ASSERT_TRUE(registry.has_value());

		ASSERT_TRUE(
			builder.NetworkRegistries().Declare(
				std::move(*registry)).has_value());

		const auto result =
			std::move(builder).Build(1);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			RegistrySnapshotErrorCode::
			RegistryRepresentationCollision);

		EXPECT_EQ(
			result.error().Registry,
			RegistryKind::DimensionType);

		EXPECT_TRUE(
			builder.NetworkRegistries().Contains(
				Location(
					"minecraft:dimension_type")));
	}
}
