#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>


namespace {
	using Aurore::Protocol::Packets::Configuration::KnownPack;
	using Aurore::Protocol::RegistryCompatibilityEvidence;
	using Aurore::Protocol::RegistryCompatibilityManifest;
	using Aurore::Protocol::RegistryCompatibilityRule;
	using Aurore::Protocol::RegistryEntryDataPolicy;
	using Aurore::Protocol::RegistryManifestCompleteness;
	using Aurore::Protocol::RegistryManifestError;
	using Aurore::Protocol::RegistryManifestErrorCode;
	using Aurore::Protocol::RegistryPresence;
	using Aurore::Protocol::RegistryRepresentation;
	using Aurore::Protocol::RegistryTagPolicy;
	using Aurore::Protocol::RegistryCompatibilityInventoryEntry;
	using Aurore::Util::ResourceLocation;

	struct RawRegistryRule final {
		std::string_view Key;

		RegistryPresence Presence;
		RegistryEntryDataPolicy EntryData;
		RegistryTagPolicy Tags;
		RegistryRepresentation Representation;
		RegistryCompatibilityEvidence Evidence;

		std::size_t MinimumEntries;

		std::string_view SchemaContract;
		std::string_view SourceCodec;
	};

	/*
		This table contains only contracts exposed by the first recorded
		vanilla-client validation. It is intentionally provisional.

		Block and item are represented because Aurore currently transmits
		their tag sections, not because their Registry Data contents are
		transmitted.
	*/
	constexpr std::array Protocol774InitialRules{
		RawRegistryRule{
			.Key = "minecraft:block",
			.Presence = RegistryPresence::Optional,
			.EntryData = RegistryEntryDataPolicy::Forbidden,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::TypedDomain,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 0,
			.SchemaContract = {},
			.SourceCodec = {},
		},
		RawRegistryRule{
			.Key = "minecraft:item",
			.Presence = RegistryPresence::Optional,
			.EntryData = RegistryEntryDataPolicy::Forbidden,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::TypedDomain,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 0,
			.SchemaContract = {},
			.SourceCodec = {},
		},
		RawRegistryRule{
			.Key = "minecraft:dimension_type",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::TypedDomain,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:dimension_type@1.21.11",
			.SourceCodec = "minecraft:dimension_type",
		},
		RawRegistryRule{
			.Key = "minecraft:worldgen/biome",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::TypedDomain,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:worldgen/biome@1.21.11",
			.SourceCodec = "minecraft:worldgen/biome",
		},
		RawRegistryRule{
			.Key = "minecraft:cat_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:cat_variant@1.21.11",
			.SourceCodec = "minecraft:cat_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:chicken_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:chicken_variant@1.21.11",
			.SourceCodec = "minecraft:chicken_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:cow_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:cow_variant@1.21.11",
			.SourceCodec = "minecraft:cow_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:frog_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:frog_variant@1.21.11",
			.SourceCodec = "minecraft:frog_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:painting_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:painting_variant@1.21.11",
			.SourceCodec = "minecraft:painting_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:pig_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:pig_variant@1.21.11",
			.SourceCodec = "minecraft:pig_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:wolf_sound_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:wolf_sound_variant@1.21.11",
			.SourceCodec = "minecraft:wolf_sound_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:wolf_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:wolf_variant@1.21.11",
			.SourceCodec = "minecraft:wolf_variant",
		},
		RawRegistryRule{
			.Key = "minecraft:zombie_nautilus_variant",
			.Presence = RegistryPresence::Required,
			.EntryData = RegistryEntryDataPolicy::Required,
			.Tags = RegistryTagPolicy::Optional,
			.Representation = RegistryRepresentation::GenericNetwork,
			.Evidence = RegistryCompatibilityEvidence::VanillaObservation,
			.MinimumEntries = 1,
			.SchemaContract = "minecraft:zombie_nautilus_variant@1.21.11",
			.SourceCodec = "minecraft:zombie_nautilus_variant",
		},
	};

	[[nodiscard]] bool IsKnownPackSelected(const KnownPack& pack, std::span<const KnownPack> selected_packs) {
		return std::ranges::find(selected_packs, pack) != selected_packs.end();
	}

	enum class TypedRegistrySource : std::uint8_t {
		Block,
		Item,
		Biome,
		DimensionType,
	};

	struct TypedRegistryDescriptor final {
		TypedRegistrySource Source;
		std::string_view Key;
	};

	constexpr std::array TypedRegistryDescriptors{
		TypedRegistryDescriptor{
			.Source = TypedRegistrySource::Block,
			.Key = "minecraft:block",
		},
		TypedRegistryDescriptor{
			.Source = TypedRegistrySource::Item,
			.Key = "minecraft:item",
		},
		TypedRegistryDescriptor{
			.Source = TypedRegistrySource::Biome,
			.Key = "minecraft:worldgen/biome",
		},
		TypedRegistryDescriptor{
			.Source = TypedRegistrySource::DimensionType,
			.Key = "minecraft:dimension_type",
		},
	};

	[[nodiscard]] const ResourceLocation& TypedRegistryKey(TypedRegistrySource source) {
		static const auto block = ResourceLocation::Parse("minecraft:block").value();
		static const auto item = ResourceLocation::Parse("minecraft:item").value();
		static const auto biome = ResourceLocation::Parse("minecraft:worldgen/biome").value();
		static const auto dimension_type = ResourceLocation::Parse("minecraft:dimension_type").value();

		switch (source) {
		case TypedRegistrySource::Block: return block;
		case TypedRegistrySource::Item: return item;
		case TypedRegistrySource::Biome: return biome;
		case TypedRegistrySource::DimensionType: return dimension_type;
		}

		std::unreachable();
	}

	[[nodiscard]] std::optional<TypedRegistrySource> FindTypedRegistrySource(const ResourceLocation& key) noexcept {
		for (const auto& descriptor : TypedRegistryDescriptors)
			if (key.ToString() == descriptor.Key)
				return descriptor.Source;
		return std::nullopt;
	}

	[[nodiscard]] RegistryCompatibilityInventoryEntry BuildTypedInventoryEntry(TypedRegistrySource source, const Aurore::Util::RegistrySnapshot& snapshot) {
		switch (source) {
		case TypedRegistrySource::Block:
			return RegistryCompatibilityInventoryEntry{
				.Key = TypedRegistryKey(source),

				/*
					Block entries are not transmitted through
					Registry Data. Only their tag section
					contributes to Configuration inventory.
				*/
				.EntryCount = 0,
				.EntriesWithData = 0,
				.TagCount = snapshot.GetBlockTags().Size(),
			};

		case TypedRegistrySource::Item:
			return RegistryCompatibilityInventoryEntry{
				.Key = TypedRegistryKey(source),
				.EntryCount = 0,
				.EntriesWithData = 0,
				.TagCount = snapshot.GetItemTags().Size(),
			};

		case TypedRegistrySource::Biome:
			return RegistryCompatibilityInventoryEntry{
				.Key = TypedRegistryKey(source),
				.EntryCount = snapshot.GetBiomes().Size(),
				.EntriesWithData = snapshot.GetBiomes().Size(),
				.TagCount = snapshot.GetBiomeTags().Size(),
			};

		case TypedRegistrySource::DimensionType:
			return RegistryCompatibilityInventoryEntry{
				.Key = TypedRegistryKey(source),
				.EntryCount = snapshot.GetDimensionTypes().Size(),
				.EntriesWithData = snapshot.GetDimensionTypes().Size(),
				.TagCount = snapshot.GetDimensionTypeTags().Size(),
			};
		}

		std::unreachable();
	}

	[[nodiscard]] RegistryCompatibilityInventoryEntry BuildGenericInventoryEntry(const Aurore::Util::NetworkRegistry& registry, const Aurore::Util::RegistrySnapshot& snapshot) {
		const auto* tags = snapshot.GetNetworkRegistryTags().Find(registry.GetKey());
		return RegistryCompatibilityInventoryEntry{
			.Key = registry.GetKey(),
			.EntryCount = registry.Size(),
			.EntriesWithData = registry.GetEntriesWithDataCount(),
			.TagCount = tags == nullptr ? 0 : tags->Size(),
		};
	}

	[[nodiscard]] bool TypedEntryContributes(const RegistryCompatibilityInventoryEntry& entry) noexcept {
		return entry.EntryCount != 0 || entry.EntriesWithData != 0 || entry.TagCount != 0;
	}
}

namespace Aurore::Protocol {
	RegistryCompatibilityManifest::RegistryCompatibilityManifest(
		std::string minecraft_version, std::int32_t protocol_version, RegistryManifestCompleteness completeness, std::vector<RegistryCompatibilityRule> rules, RuleIndex rule_index) noexcept
		: m_MinecraftVersion(std::move(minecraft_version)), m_ProtocolVersion(protocol_version), m_Completeness(completeness), m_Rules(std::move(rules)), m_RuleIndex(std::move(rule_index)) {}

	std::expected<RegistryCompatibilityManifest, RegistryManifestError> RegistryCompatibilityManifest::Create(std::string minecraft_version, std::int32_t protocol_version, RegistryManifestCompleteness completeness, std::vector<RegistryCompatibilityRule> rules) {
		if (minecraft_version.empty())
			return std::unexpected(RegistryManifestError{ .Code = RegistryManifestErrorCode::EmptyMinecraftVersion });
		if (protocol_version <= 0)
			return std::unexpected(RegistryManifestError{ .Code = RegistryManifestErrorCode::InvalidProtocolVersion });
		if (rules.empty())
			return std::unexpected(RegistryManifestError{ .Code = RegistryManifestErrorCode::EmptyManifest });

		RuleIndex rule_index;
		rule_index.reserve(rules.size());
		for (std::size_t index{ 0 }; index < rules.size(); index++) {
			const auto& rule = rules[index];
			const auto [existing, inserted] = rule_index.emplace(rule.Key, index);
			if (!inserted)
				return std::unexpected(RegistryManifestError{
					.Code = RegistryManifestErrorCode::DuplicateRegistry,
					.RuleIndex = index,
					.ExistingRuleIndex = existing->second,
					.Key = rule.Key
				});

			if (rule.EntryData == RegistryEntryDataPolicy::Forbidden) {
				if (rule.MinimumEntries != 0)
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::InvalidMinimumEntryCount,
						.RuleIndex = index,
						.Key = rule.Key,
					});

				if (rule.DataProvidedByKnownPack)
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::InvalidKnownPackReference,
						.RuleIndex = index,
						.Key = rule.Key,
					});
			}
			else {
				if (rule.SchemaContract.empty())
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::MissingSchemaContract,
						.RuleIndex = index,
						.Key = rule.Key,
					});

				if (rule.SourceCodec.empty())
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::MissingSourceCodec,
						.RuleIndex = index,
						.Key = rule.Key,
					});
			}

			if (rule.DataProvidedByKnownPack) {
				const auto& pack = *rule.DataProvidedByKnownPack;
				if (pack.Namespace.empty() || pack.Id.empty() || pack.Version.empty())
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::InvalidKnownPackReference,
						.RuleIndex = index,
						.Key = rule.Key,
					});
			}

			for (std::size_t dependency_index{ 0 }; dependency_index < rule.Dependencies.size(); ++dependency_index) {
				const auto& dependency = rule.Dependencies[dependency_index];
				if (dependency == rule.Key)
					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::SelfDependency,
						.RuleIndex = index,
						.DependencyIndex = dependency_index,
						.Key = rule.Key,
						.Dependency = dependency,
					});

				for (std::size_t existing_index{ 0 }; existing_index < dependency_index; existing_index++) {
					if (rule.Dependencies[existing_index] != dependency) continue;

					return std::unexpected(RegistryManifestError{
						.Code = RegistryManifestErrorCode::DuplicateDependency,
						.RuleIndex = index,
						.ExistingRuleIndex = existing_index,
						.DependencyIndex = dependency_index,
						.Key = rule.Key,
						.Dependency = dependency,
					});
				}
			}
		}

		for (std::size_t index{ 0 }; index < rules.size(); index++) {
			const auto& rule = rules[index];
			for (std::size_t dependency_index{ 0 }; dependency_index < rule.Dependencies.size(); dependency_index++) {
				const auto& dependency = rule.Dependencies[dependency_index];
				if (rule_index.contains(dependency)) continue;

				return std::unexpected(RegistryManifestError{
					.Code = RegistryManifestErrorCode::UnknownDependency,
					.RuleIndex = index,
					.DependencyIndex = dependency_index,
					.Key = rule.Key,
					.Dependency = dependency,
				});
			}
		}

		return RegistryCompatibilityManifest{
			std::move(minecraft_version),
			protocol_version,
			completeness,
			std::move(rules),
			std::move(rule_index),
		};
	}

	const RegistryCompatibilityRule* RegistryCompatibilityManifest::Find(const Aurore::Util::ResourceLocation& key) const noexcept {
		const auto iterator = m_RuleIndex.find(key);
		if (iterator == m_RuleIndex.end()) return nullptr;
		return &m_Rules[iterator->second];
	}

	std::expected<void, RegistryInventoryError> RegistryCompatibilityValidator::Validate(const RegistryCompatibilityManifest& manifest,
		std::span<const RegistryCompatibilityInventoryEntry> inventory, std::span<const Packets::Configuration::KnownPack> selected_known_packs) {
		using InventoryIndex = std::unordered_map<Aurore::Util::ResourceLocation, std::size_t>;

		InventoryIndex inventory_index;
		inventory_index.reserve(inventory.size());
		for (std::size_t index{ 0 }; index < inventory.size(); index++) {
			const auto& entry = inventory[index];
			if (entry.EntriesWithData > entry.EntryCount)
				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::InvalidEntryDataCount,
					.Key = entry.Key,
					.InventoryIndex = index,
					.ObservedValue = entry.EntriesWithData,
					.RequiredValue = entry.EntryCount,
				});

			const auto [existing, inserted] = inventory_index.emplace(entry.Key, index);
			if (!inserted)
				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::DuplicateRegistry,
					.Key = entry.Key,
					.InventoryIndex = index,
					.ExistingInventoryIndex = existing->second,
				});

			if (manifest.Find(entry.Key) == nullptr && manifest.GetCompleteness() == RegistryManifestCompleteness::Complete)
				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::UnexpectedRegistry,
					.Key = entry.Key,
					.InventoryIndex = index,
				});
		}

		const auto rules = manifest.GetRules();
		for (std::size_t rule_index{ 0 }; rule_index < rules.size(); rule_index++) {
			const auto& rule = rules[rule_index];
			const auto inventory_iterator = inventory_index.find(rule.Key);

			if (inventory_iterator == inventory_index.end()) {
				if (rule.Presence == RegistryPresence::Optional)
					continue;

				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::MissingRequiredRegistry,
					.Key = rule.Key,
					.RuleIndex = rule_index,
					.RequiredValue = rule.MinimumEntries,
				});
			}

			const auto entry_index = inventory_iterator->second;
			const auto& entry = inventory[entry_index];

			if (entry.EntryCount < rule.MinimumEntries)
				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::MinimumEntryCountNotMet,
					.Key = rule.Key,
					.RuleIndex = rule_index,
					.InventoryIndex = entry_index,
					.ObservedValue = entry.EntryCount,
					.RequiredValue = rule.MinimumEntries,
				});

			switch (rule.EntryData) {
			case RegistryEntryDataPolicy::Forbidden:
				if (entry.EntryCount != 0 || entry.EntriesWithData != 0)
					return std::unexpected(RegistryInventoryError{
						.Code = RegistryInventoryErrorCode::EntryDataForbidden,
						.Key = rule.Key,
						.RuleIndex = rule_index,
						.InventoryIndex = entry_index,
						.ObservedValue = entry.EntryCount,
					});
				break;
			case RegistryEntryDataPolicy::Optional: break;
			case RegistryEntryDataPolicy::Required: {
				const bool data_may_be_omitted = rule.DataProvidedByKnownPack && IsKnownPackSelected(*rule.DataProvidedByKnownPack, selected_known_packs);
				if (!data_may_be_omitted && entry.EntriesWithData != entry.EntryCount)
					return std::unexpected(RegistryInventoryError{
						.Code = RegistryInventoryErrorCode::MissingRequiredEntryData,
						.Key = rule.Key,
						.RuleIndex = rule_index,
						.InventoryIndex = entry_index,
						.ObservedValue = entry.EntriesWithData,
						.RequiredValue = entry.EntryCount,
					});
			} break;
			}

			switch (rule.Tags) {
			case RegistryTagPolicy::Forbidden:
				if (entry.TagCount != 0)
					return std::unexpected(RegistryInventoryError{
						.Code = RegistryInventoryErrorCode::TagsForbidden,
						.Key = rule.Key,
						.RuleIndex = rule_index,
						.InventoryIndex = entry_index,
						.ObservedValue = entry.TagCount,
					});
				break;
			case RegistryTagPolicy::Optional: break;
			case RegistryTagPolicy::Required:
				if (entry.TagCount == 0)
					return std::unexpected(RegistryInventoryError{
						.Code = RegistryInventoryErrorCode::MissingRequiredTags,
						.Key = rule.Key,
						.RuleIndex = rule_index,
						.InventoryIndex = entry_index,
						.RequiredValue = 1,
					});
				break;
			}

			for (const auto& dependency : rule.Dependencies) {
				if (inventory_index.contains(dependency)) continue;
				return std::unexpected(RegistryInventoryError{
					.Code = RegistryInventoryErrorCode::MissingDependency,
					.Key = rule.Key,
					.Dependency = dependency,
					.RuleIndex = rule_index,
					.InventoryIndex = entry_index,
				});
			}
		}

		return {};
	}

	std::expected<RegistryCompatibilityManifest, RegistryManifestError> Protocol774RegistryManifest::Build() {
		std::vector<RegistryCompatibilityRule> rules;
		rules.reserve(Protocol774InitialRules.size());
		for (std::size_t index{ 0 }; index < Protocol774InitialRules.size(); index++) {
			const auto& raw = Protocol774InitialRules[index];
			auto key = Aurore::Util::ResourceLocation::Parse(raw.Key);
			if (!key)
				return std::unexpected(RegistryManifestError{
					.Code = RegistryManifestErrorCode::InvalidRegistryKey,
					.RuleIndex = index,
					.LocationError = key.error(),
					.InvalidValue = std::string{ raw.Key }
				});

			rules.push_back(RegistryCompatibilityRule{
				.Key = std::move(*key),
				.Presence = raw.Presence,
				.EntryData = raw.EntryData,
				.Tags = raw.Tags,
				.Representation = raw.Representation,
				.Evidence = raw.Evidence,
				.MinimumEntries = raw.MinimumEntries,
				.SchemaContract =
					std::string{ raw.SchemaContract },
				.SourceCodec = std::string{ raw.SourceCodec },
				.DataProvidedByKnownPack = std::nullopt,
				.Dependencies = {},
			});
		}
		return RegistryCompatibilityManifest::Create(
			"1.21.11",
			Packets::Configuration::TargetProtocolVersion,
			RegistryManifestCompleteness::Provisional,
			std::move(rules));
	}

	std::expected<std::vector<RegistryCompatibilityInventoryEntry>, RegistrySnapshotInventoryError> RegistrySnapshotInventoryAdapter::Build(const RegistryCompatibilityManifest& manifest, const Aurore::Util::RegistrySnapshot& snapshot) {
		std::vector<RegistryCompatibilityInventoryEntry> inventory;
		inventory.reserve(manifest.GetRules().size() + snapshot.GetNetworkRegistries().Size() + TypedRegistryDescriptors.size());
		const auto rules = manifest.GetRules();
		for (std::size_t i{ 0 }; i < rules.size(); i++) {
			const auto& rule = rules[i];
			const auto typed_source = FindTypedRegistrySource(rule.Key);
			if (rule.Representation == RegistryRepresentation::TypedDomain) {
				if (!typed_source) {
					const bool exists_as_generic = snapshot.GetNetworkRegistries().Contains(rule.Key);
					return std::unexpected(RegistrySnapshotInventoryError{
						.Code = exists_as_generic
								? RegistrySnapshotInventoryErrorCode::RepresentationMismatch
								: RegistrySnapshotInventoryErrorCode::UnsupportedTypedRegistry,
						.Key = rule.Key,
						.RuleIndex = i,
						.ExpectedRepresentation = RegistryRepresentation::TypedDomain,
						.ObservedRepresentation = exists_as_generic
								? std::optional{RegistryRepresentation::GenericNetwork}
								: std::nullopt,
					});
				}

				inventory.push_back(BuildTypedInventoryEntry(*typed_source, snapshot));
				continue;
			}

			if (typed_source)
				return std::unexpected(RegistrySnapshotInventoryError{
					.Code = RegistrySnapshotInventoryErrorCode::RepresentationMismatch,
					.Key = rule.Key,
					.RuleIndex = i,
					.ExpectedRepresentation = RegistryRepresentation::GenericNetwork,
					.ObservedRepresentation = RegistryRepresentation::TypedDomain,
				});

			const auto* registry = snapshot.GetNetworkRegistries().Find(rule.Key);

			/*
				An absent generic registry is deliberately omitted.
				RegistryCompatibilityValidator determines whether
				the absence violates Required or dependency policy.
			*/
			if (registry == nullptr) continue;
			inventory.push_back(BuildGenericInventoryEntry(*registry, snapshot));
		}

		/*
			Include typed Configuration sections not classified by
			the manifest. This allows a Complete manifest to reject
			them as unexpected rather than silently dropping them.
		*/
		for (const auto& descriptor : TypedRegistryDescriptors) {
			const auto& key = TypedRegistryKey(descriptor.Source);
			if (manifest.Find(key) != nullptr) continue;

			auto entry = BuildTypedInventoryEntry(descriptor.Source, snapshot);
			if (TypedEntryContributes(entry))
				inventory.push_back(std::move(entry));
		}

		/*
			A provisional manifest may not yet classify every generic
			registry. Retain those registries in snapshot order so the
			validator can permit or reject them according to manifest
			completeness.
		*/
		for (const auto& registry : snapshot.GetNetworkRegistries().GetRegistries()) {
			if (manifest.Find(registry.GetKey()) != nullptr) continue;
			inventory.push_back(BuildGenericInventoryEntry(registry, snapshot));
		}

		return inventory;
	}
}
