#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Aurore::Tests {
	namespace {
		namespace Configuration = Aurore::Protocol::Packets::Configuration;

		using Aurore::Core::SyntheticRegistrySnapshotFactory;
		using Aurore::Protocol::ConfigurationSequenceBuilder;
		using Aurore::Protocol::ConfigurationSequenceErrorCode;
		using Aurore::Protocol::ConfigurationSequencePolicy;
		using Aurore::Protocol::RegistryCompatibilityEvidence;
		using Aurore::Protocol::RegistryCompatibilityManifest;
		using Aurore::Protocol::RegistryCompatibilityRule;
		using Aurore::Protocol::RegistryEntryDataPolicy;
		using Aurore::Protocol::RegistryManifestCompleteness;
		using Aurore::Protocol::RegistryPresence;
		using Aurore::Protocol::RegistryRepresentation;
		using Aurore::Protocol::RegistryTagPolicy;
		using Aurore::Util::NbtCompound;
		using Aurore::Util::NbtString;
		using Aurore::Util::NbtValue;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::RegistrySnapshotBuilder;
		using Aurore::Util::ResourceLocation;

		[[nodiscard]] ResourceLocation Location(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			if (!result) throw std::runtime_error("Invalid ResourceLocation fixture.");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistryCompatibilityManifest> BuiltInManifest() {
			auto manifest = Aurore::Protocol::Protocol774RegistryManifest::Build();
			if (!manifest) throw std::runtime_error("Failed to build protocol-774 manifest fixture.");
			return std::make_shared<const RegistryCompatibilityManifest>(std::move(*manifest));
		}

		[[nodiscard]] ConfigurationSequencePolicy MakePolicy() {
			return ConfigurationSequencePolicy{
				.EnabledFeatures = { Location("minecraft:vanilla") },
				.KnownPacks = {},
				.RegistryManifest = BuiltInManifest(),
			};
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> SyntheticSnapshot() {
			auto snapshot = SyntheticRegistrySnapshotFactory::Build(1);
			if (!snapshot) throw std::runtime_error("Failed to build synthetic snapshot fixture.");
			return std::move(*snapshot);
		}

		[[nodiscard]] RegistryCompatibilityRule GenericRule(std::string_view key) {
			return RegistryCompatibilityRule{
				.Key = Location(key),
				.Presence = RegistryPresence::Required,
				.EntryData = RegistryEntryDataPolicy::Required,
				.Tags = RegistryTagPolicy::Optional,
				.Representation = RegistryRepresentation::GenericNetwork,
				.Evidence = RegistryCompatibilityEvidence::ProtocolContract,
				.MinimumEntries = 1,
				.SchemaContract = "aurore_test:generic@1",
				.SourceCodec = "aurore_test:generic",
			};
		}

		[[nodiscard]] std::shared_ptr<const RegistryCompatibilityManifest> SingleRegistryManifest(RegistryCompatibilityRule rule) {
			auto manifest = RegistryCompatibilityManifest::Create(
				"1.21.11", Configuration::TargetProtocolVersion, RegistryManifestCompleteness::Complete, { std::move(rule) });
			if (!manifest) throw std::runtime_error("Failed to build registry manifest fixture.");
			return std::make_shared<const RegistryCompatibilityManifest>(std::move(*manifest));
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> GenericSnapshot(const ResourceLocation& registry_key, std::optional<NbtCompound> data) {
			RegistrySnapshotBuilder builder;
			auto registry = builder.NetworkRegistries().CreateRegistryBuilder(registry_key);
			if (!registry.Declare(Location("aurore_test:entry"), std::move(data)))
				throw std::runtime_error("Failed to declare generic registry entry.");
			auto built = std::move(registry).Build();
			if (!built || !builder.NetworkRegistries().Declare(std::move(*built)))
				throw std::runtime_error("Failed to build generic registry fixture.");
			auto snapshot = std::move(builder).Build(1);
			if (!snapshot) throw std::runtime_error("Failed to build generic snapshot fixture.");
			return std::move(*snapshot);
		}

		[[nodiscard]] NbtCompound Data() {
			NbtCompound data;
			data.Set(NbtString{ u"value" }, NbtValue::Int(1));
			return data;
		}
	}

	TEST(ConfigurationSequenceTests, RejectsMissingSnapshot) {
		const auto result = ConfigurationSequenceBuilder::Build(nullptr, MakePolicy());
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, ConfigurationSequenceErrorCode::MissingSnapshot);
	}

	TEST(ConfigurationSequenceTests, RejectsMissingManifest) {
		auto policy = MakePolicy();
		policy.RegistryManifest.reset();
		const auto result = ConfigurationSequenceBuilder::Build(SyntheticSnapshot(), policy);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, ConfigurationSequenceErrorCode::MissingRegistryManifest);
	}

	TEST(ConfigurationSequenceTests, BuildsOnlyNegotiationFramesInitially) {
		auto plan = ConfigurationSequenceBuilder::Build(SyntheticSnapshot(), MakePolicy());
		ASSERT_TRUE(plan.has_value());
		ASSERT_EQ(plan->GetInitialFrames().size(), 2u);
		EXPECT_EQ(plan->GetInitialFrames()[0].PacketId, Configuration::Clientbound::FeatureFlagsPacketId);
		EXPECT_EQ(plan->GetInitialFrames()[1].PacketId, Configuration::Clientbound::SelectKnownPacksPacketId);
	}

	TEST(ConfigurationSequenceTests, MaterializesSyntheticDataAfterKnownPackSelection) {
		auto plan = ConfigurationSequenceBuilder::Build(SyntheticSnapshot(), MakePolicy());
		ASSERT_TRUE(plan.has_value());
		ASSERT_EQ(plan->ReleaseInitialFrames().size(), 2u);

		auto frames = plan->MaterializePostNegotiationFrames({});
		ASSERT_TRUE(frames.has_value());
		ASSERT_EQ(frames->size(), 4u);
		EXPECT_EQ((*frames)[0].PacketId, Configuration::Clientbound::RegistryDataPacketId);
		EXPECT_EQ((*frames)[1].PacketId, Configuration::Clientbound::RegistryDataPacketId);
		EXPECT_EQ((*frames)[2].PacketId, Configuration::Clientbound::TagsPacketId);
		EXPECT_EQ((*frames)[3].PacketId, Configuration::Clientbound::FinishConfigurationPacketId);
	}

	TEST(ConfigurationSequenceTests, GenericRegistryNeedsNoSequenceBranch) {
		const auto key = Location("aurore_test:dynamic_registry");
		ConfigurationSequencePolicy policy{
			.EnabledFeatures = { Location("minecraft:vanilla") },
			.RegistryManifest = SingleRegistryManifest(GenericRule("aurore_test:dynamic_registry")),
		};

		auto plan = ConfigurationSequenceBuilder::Build(GenericSnapshot(key, Data()), policy);
		ASSERT_TRUE(plan.has_value());
		ASSERT_EQ(plan->ReleaseInitialFrames().size(), 2u);

		auto frames = plan->MaterializePostNegotiationFrames({});
		ASSERT_TRUE(frames.has_value());
		ASSERT_EQ(frames->size(), 3u);
		EXPECT_EQ((*frames)[0].PacketId, Configuration::Clientbound::RegistryDataPacketId);
		EXPECT_EQ((*frames)[1].PacketId, Configuration::Clientbound::TagsPacketId);
		EXPECT_EQ((*frames)[2].PacketId, Configuration::Clientbound::FinishConfigurationPacketId);
	}

	TEST(ConfigurationSequenceTests, SelectedKnownPackAllowsOmittedEntryPayload) {
		const Configuration::KnownPack pack{ .Namespace = "minecraft", .Id = "core", .Version = "1.21.11" };
		auto rule = GenericRule("aurore_test:pack_registry");
		rule.DataProvidedByKnownPack = pack;

		ConfigurationSequencePolicy policy{
			.EnabledFeatures = { Location("minecraft:vanilla") },
			.KnownPacks = { pack },
			.RegistryManifest = SingleRegistryManifest(std::move(rule)),
		};

		auto plan = ConfigurationSequenceBuilder::Build(
			GenericSnapshot(Location("aurore_test:pack_registry"), std::nullopt), policy);
		ASSERT_TRUE(plan.has_value());
		ASSERT_EQ(plan->ReleaseInitialFrames().size(), 2u);

		const std::array selected{ pack };
		const auto frames = plan->MaterializePostNegotiationFrames(selected);
		EXPECT_TRUE(frames.has_value());
	}

	TEST(ConfigurationSequenceTests, MissingRequiredPayloadFailsWithoutKnownPack) {
		const Configuration::KnownPack pack{ .Namespace = "minecraft", .Id = "core", .Version = "1.21.11" };
		auto rule = GenericRule("aurore_test:pack_registry");
		rule.DataProvidedByKnownPack = pack;

		ConfigurationSequencePolicy policy{
			.EnabledFeatures = { Location("minecraft:vanilla") },
			.KnownPacks = { pack },
			.RegistryManifest = SingleRegistryManifest(std::move(rule)),
		};

		auto plan = ConfigurationSequenceBuilder::Build(
			GenericSnapshot(Location("aurore_test:pack_registry"), std::nullopt), policy);
		ASSERT_TRUE(plan.has_value());
		ASSERT_EQ(plan->ReleaseInitialFrames().size(), 2u);

		const auto frames = plan->MaterializePostNegotiationFrames({});
		ASSERT_FALSE(frames.has_value());
		EXPECT_EQ(frames.error().Code, ConfigurationSequenceErrorCode::RegistryValidationFailed);
	}
}
