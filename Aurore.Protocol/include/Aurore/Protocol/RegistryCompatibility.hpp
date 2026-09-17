#pragma once

#include <Aurore/Protocol/Packets/Configuration.hpp>

#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/RegistrySnapshot.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace Aurore::Protocol {
	enum class RegistryManifestCompleteness : std::uint8_t {
		Provisional,
		Complete,
	};

	enum class RegistryPresence : std::uint8_t {
		Optional,
		Required,
	};

	enum class RegistryEntryDataPolicy : std::uint8_t {
		// No registry Data packet is expected for this registry.
		// The registry may still have a section in Update Tags.
		Forbidden,

		// Registry Data entries may contain either present or absent entry data.
		Optional,

		// Every transmitted entry must contain data unless the entry data
		// is supplied by a selected Known Pack.
		Required
	};

	enum class RegistryTagPolicy : std::uint8_t {
		Forbidden,
		Optional,
		Required,
	};

	enum class RegistryRepresentation : std::uint8_t {
		// Aurore owns a dedicated semantic C++ model for this registry.
		TypedDomain,

		// The registry is retained as ordered immutable wire data and
		// does not require a bespoke domain type.
		GenericNetwork,
	};

	enum class RegistryCompatibilityEvidence : std::uint8_t {
		// The rule has been independently established as part of the
		// target protocol contract.
		ProtocolContract,

		// The rule is currently based on a recorded unmodified-client
		// compatibility observation.
		VanillaObservation,
	};

	struct RegistryCompatibilityRule final {
		Aurore::Util::ResourceLocation Key;

		RegistryPresence Presence{ RegistryPresence::Optional };
		RegistryEntryDataPolicy EntryData{ RegistryEntryDataPolicy::Forbidden };
		RegistryTagPolicy Tags{ RegistryTagPolicy::Optional };
		RegistryRepresentation Representation{ RegistryRepresentation::GenericNetwork };
		RegistryCompatibilityEvidence Evidence{ RegistryCompatibilityEvidence::VanillaObservation };

		// Minimum number of transmitted Registry Data entries. This
		// must remain zero when EntryData is Forbidden.
		std::size_t MinimumEntries{ 0 };

		// Stable identifier for the entry schema expected by the target
		// client. This is an Aurore contract identifier, not copied game
		// data.
		std::string SchemaContract;

		// Identifier for the authoritative source codec or extraction
		// rule used by the future user-local generator.
		std::string SourceCodec;

		// When present, entry data may be omitted only if this exact
		// Known Pack was offered and selected.
		std::optional<Packets::Configuration::KnownPack> DataProvidedByKnownPack;

		// Other wire registries that must be present whenever this
		// registry is present.
		std::vector<Aurore::Util::ResourceLocation> Dependencies;

		auto operator<=>(const RegistryCompatibilityRule&) const noexcept = default;
	};

	enum class RegistryManifestErrorCode : std::uint8_t {
		EmptyMinecraftVersion,
		InvalidProtocolVersion,
		EmptyManifest,

		InvalidRegistryKey,
		DuplicateRegistry,

		InvalidMinimumEntryCount,
		MissingSchemaContract,
		MissingSourceCodec,
		InvalidKnownPackReference,

		SelfDependency,
		DuplicateDependency,
		UnknownDependency,
	};

	struct RegistryManifestError final {
		RegistryManifestErrorCode Code;

		std::size_t RuleIndex{ 0 };
		std::optional<std::size_t> ExistingRuleIndex{};
		std::optional<std::size_t> DependencyIndex{};

		std::optional<Aurore::Util::ResourceLocation> Key{};
		std::optional<Aurore::Util::ResourceLocation> Dependency{};
		std::optional<Aurore::Util::ResourceLocationError> LocationError{};

		std::string InvalidValue;

		auto operator<=>(const RegistryManifestError&) const noexcept = default;
	};

	class RegistryCompatibilityManifest final {
	public:
		using RuleIndex = std::unordered_map<Aurore::Util::ResourceLocation, std::size_t>;

		[[nodiscard]] static std::expected<RegistryCompatibilityManifest, RegistryManifestError> Create(
			std::string minecraft_version, std::int32_t protocol_version, RegistryManifestCompleteness completeness, std::vector<RegistryCompatibilityRule> rules);

		[[nodiscard]] std::string_view GetMinecraftVersion() const noexcept { return m_MinecraftVersion; }
		[[nodiscard]] std::int32_t GetProtocolVersion() const noexcept { return m_ProtocolVersion; }
		[[nodiscard]] RegistryManifestCompleteness GetCompleteness() const noexcept { return m_Completeness; }
		[[nodiscard]] std::span<const RegistryCompatibilityRule> GetRules() const noexcept { return m_Rules; }

		[[nodiscard]] const RegistryCompatibilityRule* Find(const Aurore::Util::ResourceLocation& key) const noexcept;

	private:
		RegistryCompatibilityManifest(std::string minecraft_version, std::int32_t protocol_version, RegistryManifestCompleteness completeness, std::vector<RegistryCompatibilityRule> rules, RuleIndex rule_index) noexcept;

		std::string m_MinecraftVersion;
		std::int32_t m_ProtocolVersion;
		RegistryManifestCompleteness m_Completeness;

		std::vector<RegistryCompatibilityRule> m_Rules;
		RuleIndex m_RuleIndex;
	};

	/*
		A structural view of a generated or retained Configuration dataset.

		This deliberately does not expose RegistrySnapshot or a future generic
		network-registry implementation. Both can construct the same inventory.
	*/
	struct RegistryCompatibilityInventoryEntry final {
		Aurore::Util::ResourceLocation Key;

		std::size_t EntryCount{ 0 };
		std::size_t EntriesWithData{ 0 };
		std::size_t TagCount{ 0 };

		auto operator<=>(const RegistryCompatibilityInventoryEntry&) const noexcept = default;
	};

	enum class RegistryInventoryErrorCode : std::uint8_t {
		DuplicateRegistry,
		UnexpectedRegistry,
		InvalidEntryDataCount,

		MissingRequiredRegistry,
		MinimumEntryCountNotMet,
		MissingRequiredEntryData,
		EntryDataForbidden,

		MissingRequiredTags,
		TagsForbidden,

		MissingDependency,
	};

	struct RegistryInventoryError final {
		RegistryInventoryErrorCode Code;

		std::optional<Aurore::Util::ResourceLocation> Key{};
		std::optional<Aurore::Util::ResourceLocation> Dependency{};

		std::optional<std::size_t> RuleIndex{};
		std::optional<std::size_t> InventoryIndex{};
		std::optional<std::size_t> ExistingInventoryIndex{};

		std::size_t ObservedValue{ 0 };
		std::size_t RequiredValue{ 0 };

		auto operator<=>(const RegistryInventoryError&) const noexcept = default;
	};

	enum class RegistrySnapshotInventoryErrorCode
		: std::uint8_t {

		UnsupportedTypedRegistry,
		RepresentationMismatch,
	};

	struct RegistrySnapshotInventoryError final {
		RegistrySnapshotInventoryErrorCode Code;
		Aurore::Util::ResourceLocation Key;

		std::size_t RuleIndex{ 0 };

		RegistryRepresentation ExpectedRepresentation{
			RegistryRepresentation::GenericNetwork
		};

		std::optional<RegistryRepresentation>
			ObservedRepresentation{};

		auto operator<=>(
			const RegistrySnapshotInventoryError&)
			const noexcept = default;
	};

	class RegistrySnapshotInventoryAdapter final {
	public:
		RegistrySnapshotInventoryAdapter() = delete;

		[[nodiscard]] static std::expected<std::vector<RegistryCompatibilityInventoryEntry>, RegistrySnapshotInventoryError> Build(
			const RegistryCompatibilityManifest& manifest, const Aurore::Util::RegistrySnapshot& snapshot);
	};

	class RegistryCompatibilityValidator final {
	public:
		RegistryCompatibilityValidator() = delete;

		[[nodiscard]] static std::expected<void, RegistryInventoryError> Validate(
			const RegistryCompatibilityManifest& manifest, std::span<const RegistryCompatibilityInventoryEntry> inventory, std::span<const Packets::Configuration::KnownPack> selected_known_packs = {});
	};

	class Protocol774RegistryManifest final {
	public:
		Protocol774RegistryManifest() = delete;

		[[nodiscard]] static std::expected<RegistryCompatibilityManifest, RegistryManifestError> Build();
	};
}
