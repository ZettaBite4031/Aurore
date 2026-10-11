#include <Aurore/Protocol/ConfigurationRegistryAdapter.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace {
	namespace Configuration = Aurore::Protocol::Packets::Configuration;

	using Aurore::Protocol::ConfigurationRegistrySection;
	using Aurore::Protocol::RegistryCompatibilityRule;
	using Aurore::Protocol::RegistryEntryDataPolicy;
	using Aurore::Protocol::RegistryRepresentation;
	using Aurore::Protocol::RegistrySnapshotInventoryError;
	using Aurore::Protocol::RegistrySnapshotInventoryErrorCode;
	using Aurore::Protocol::RegistryTagPolicy;
	using Aurore::Util::Registry;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::RegistryTagSet;
	using Aurore::Util::ResourceLocation;

	enum class TypedRegistrySource : std::uint8_t {
		Block,
		Item,
		Biome,
		DimensionType,
	};

	struct TypedRegistryDescriptor final {
		TypedRegistrySource Source;
		std::string_view Namespace;
		std::string_view Path;
	};

	constexpr std::array TypedRegistryDescriptors{
		TypedRegistryDescriptor{ TypedRegistrySource::Block, "minecraft", "block" },
		TypedRegistryDescriptor{ TypedRegistrySource::Item, "minecraft", "item" },
		TypedRegistryDescriptor{ TypedRegistrySource::Biome, "minecraft", "worldgen/biome" },
		TypedRegistryDescriptor{ TypedRegistrySource::DimensionType, "minecraft", "dimension_type" },
	};

	[[nodiscard]] std::optional<TypedRegistrySource> FindTypedRegistrySource(const ResourceLocation& key) noexcept {
		for (const auto& descriptor : TypedRegistryDescriptors)
			if (key.GetNamespace() == descriptor.Namespace && key.GetPath() == descriptor.Path)
				return descriptor.Source;
		return std::nullopt;
	}

	[[nodiscard]] bool IsSelected(const Configuration::KnownPack& pack, std::span<const Configuration::KnownPack> selected) {
		return std::ranges::find(selected, pack) != selected.end();
	}

	template<typename T>
	[[nodiscard]] Configuration::Clientbound::RegistryData BuildRegistryData(
		const ResourceLocation& key, const Registry<T>& registry, bool omit_payloads) {

		Configuration::Clientbound::RegistryData result{ .RegistryKey = key };
		result.Entries.reserve(registry.Size());

		for (const auto& entry : registry.GetEntries())
			result.Entries.push_back({
				.Key = entry.Key,
				.Value = omit_payloads ? std::optional<Aurore::Util::NbtCompound>{} : std::optional{ entry.Value.Data },
			});

		return result;
	}

	[[nodiscard]] Configuration::Clientbound::RegistryData BuildRegistryData(
		const ResourceLocation& key, const Aurore::Util::NetworkRegistry& registry, bool omit_payloads) {

		Configuration::Clientbound::RegistryData result{ .RegistryKey = key };
		result.Entries.reserve(registry.Size());

		for (const auto& entry : registry.GetEntries())
			result.Entries.push_back({
				.Key = entry.Key,
				.Value = omit_payloads ? Aurore::Util::NetworkRegistryValue{} : entry.Value,
			});

		return result;
	}

	template<typename T>
	[[nodiscard]] Configuration::Clientbound::RegistryTagData BuildTags(
		const ResourceLocation& key, const RegistryTagSet<T>& tags) {

		Configuration::Clientbound::RegistryTagData result{ .RegistryKey = key };
		result.Tags.reserve(tags.Size());

		for (const auto& tag : tags.GetTags())
			result.Tags.push_back({ .Key = tag.Key, .Members = tag.Members });

		return result;
	}

	[[nodiscard]] Configuration::Clientbound::RegistryTagData BuildTags(
		const ResourceLocation& key, const Aurore::Util::NetworkRegistryTagSection* tags) {

		Configuration::Clientbound::RegistryTagData result{ .RegistryKey = key };
		if (!tags) return result;

		result.Tags.reserve(tags->Size());
		for (const auto& tag : tags->GetTags())
			result.Tags.push_back({ .Key = tag.Key, .Members = tag.Members });

		return result;
	}

	[[nodiscard]] RegistrySnapshotInventoryError RepresentationError(
		const RegistryCompatibilityRule& rule,
		RegistryRepresentation expected,
		std::optional<RegistryRepresentation> observed = std::nullopt) {

		return RegistrySnapshotInventoryError{
			.Code = observed ? RegistrySnapshotInventoryErrorCode::RepresentationMismatch : RegistrySnapshotInventoryErrorCode::UnsupportedTypedRegistry,
			.Key = rule.Key,
			.RuleIndex = 0,
			.ExpectedRepresentation = expected,
			.ObservedRepresentation = observed,
		};
	}
}

namespace Aurore::Protocol {
	std::expected<ConfigurationRegistrySection, RegistrySnapshotInventoryError> ConfigurationRegistryAdapter::Build(
		const RegistryCompatibilityRule& rule,
		const RegistrySnapshot& snapshot,
		std::span<const Configuration::KnownPack> selected_known_packs) {

		const auto typed = FindTypedRegistrySource(rule.Key);
		const bool omit_payloads = rule.DataProvidedByKnownPack && IsSelected(*rule.DataProvidedByKnownPack, selected_known_packs);
		ConfigurationRegistrySection result;

		if (rule.Representation == RegistryRepresentation::TypedDomain) {
			if (!typed) {
				const auto observed = snapshot.GetNetworkRegistries().Contains(rule.Key)
					? std::optional{ RegistryRepresentation::GenericNetwork }
					: std::nullopt;
				return std::unexpected(RepresentationError(rule, RegistryRepresentation::TypedDomain, observed));
			}

			switch (*typed) {
			case TypedRegistrySource::Block:
				if (rule.EntryData != RegistryEntryDataPolicy::Forbidden)
					result.RegistryData = BuildRegistryData(rule.Key, snapshot.GetBlocks(), omit_payloads);
				if (rule.Tags == RegistryTagPolicy::Required || (rule.Tags == RegistryTagPolicy::Optional && !snapshot.GetBlockTags().Empty()))
					result.Tags = BuildTags(rule.Key, snapshot.GetBlockTags());
				break;
			case TypedRegistrySource::Item:
				if (rule.EntryData != RegistryEntryDataPolicy::Forbidden)
					result.RegistryData = BuildRegistryData(rule.Key, snapshot.GetItems(), omit_payloads);
				if (rule.Tags == RegistryTagPolicy::Required || (rule.Tags == RegistryTagPolicy::Optional && !snapshot.GetItemTags().Empty()))
					result.Tags = BuildTags(rule.Key, snapshot.GetItemTags());
				break;
			case TypedRegistrySource::Biome:
				if (rule.EntryData != RegistryEntryDataPolicy::Forbidden)
					result.RegistryData = BuildRegistryData(rule.Key, snapshot.GetBiomes(), omit_payloads);
				if (rule.Tags == RegistryTagPolicy::Required || (rule.Tags == RegistryTagPolicy::Optional && !snapshot.GetBiomeTags().Empty()))
					result.Tags = BuildTags(rule.Key, snapshot.GetBiomeTags());
				break;
			case TypedRegistrySource::DimensionType:
				if (rule.EntryData != RegistryEntryDataPolicy::Forbidden)
					result.RegistryData = BuildRegistryData(rule.Key, snapshot.GetDimensionTypes(), omit_payloads);
				if (rule.Tags == RegistryTagPolicy::Required || (rule.Tags == RegistryTagPolicy::Optional && !snapshot.GetDimensionTypeTags().Empty()))
					result.Tags = BuildTags(rule.Key, snapshot.GetDimensionTypeTags());
				break;
			}

			return result;
		}

		if (typed)
			return std::unexpected(RepresentationError(rule, RegistryRepresentation::GenericNetwork, RegistryRepresentation::TypedDomain));

		const auto* registry = snapshot.GetNetworkRegistries().Find(rule.Key);
		if (!registry) return result;

		if (rule.EntryData != RegistryEntryDataPolicy::Forbidden)
			result.RegistryData = BuildRegistryData(rule.Key, *registry, omit_payloads);
		const auto* tags = snapshot.GetNetworkRegistryTags().Find(rule.Key);
		if (rule.Tags == RegistryTagPolicy::Required || (rule.Tags == RegistryTagPolicy::Optional && tags && !tags->Empty()))
			result.Tags = BuildTags(rule.Key, tags);

		return result;
	}
}
