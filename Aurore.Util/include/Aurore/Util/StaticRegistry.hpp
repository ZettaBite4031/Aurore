#pragma once

#include "Registry.hpp"
#include "RegistryTag.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Util {
	using StaticRegistryValue = std::monostate;

	class StaticRegistryBuilder;
	class StaticRegistryTagSectionBuilder;

	class StaticRegistry final {
	public:
		using Entry = RegistryEntry<StaticRegistryValue>;

		StaticRegistry(const StaticRegistry&) = delete;
		StaticRegistry& operator=(const StaticRegistry&) = delete;
		StaticRegistry(StaticRegistry&&) noexcept = default;
		StaticRegistry& operator=(StaticRegistry&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetKey() const noexcept { return m_Key; }
		[[nodiscard]] bool Empty() const noexcept { return m_Entries.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.Size(); }
		[[nodiscard]] std::span<const Entry> GetEntries() const noexcept { return m_Entries.GetEntries(); }
		[[nodiscard]] const Entry* Find(const ResourceLocation& key) const noexcept { return m_Entries.Find(key); }
		[[nodiscard]] const Entry* Find(RegistryRuntimeId runtime_id) const noexcept { return m_Entries.Find(runtime_id); }
		[[nodiscard]] std::optional<RegistryRuntimeId> FindRuntimeId(const ResourceLocation& key) const noexcept { return m_Entries.FindRuntimeId(key); }
		[[nodiscard]] const ResourceLocation* FindKey(RegistryRuntimeId runtime_id) const noexcept { return m_Entries.FindKey(runtime_id); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Entries.Contains(key); }

	private:
		using EntryRegistry = Registry<StaticRegistryValue>;

		StaticRegistry(ResourceLocation key, EntryRegistry entries) noexcept;

		ResourceLocation m_Key;
		EntryRegistry m_Entries;

		friend class StaticRegistryBuilder;
		friend class StaticRegistryTagSectionBuilder;
	};

	class StaticRegistryBuilder final {
	public:
		using DeclarationView = RegistryDeclarationView<StaticRegistryValue>;

		explicit StaticRegistryBuilder(ResourceLocation key, RegistryLimits limits = {}) noexcept;

		StaticRegistryBuilder(const StaticRegistryBuilder&) = delete;
		StaticRegistryBuilder& operator=(const StaticRegistryBuilder&) = delete;
		StaticRegistryBuilder(StaticRegistryBuilder&&) noexcept = default;
		StaticRegistryBuilder& operator=(StaticRegistryBuilder&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetKey() const noexcept { return m_Key; }
		[[nodiscard]] RegistryLimits GetLimits() const noexcept { return m_Entries.GetLimits(); }
		[[nodiscard]] bool Empty() const noexcept { return m_Entries.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.Size(); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Entries.Contains(key); }
		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept { return m_Entries.GetDeclarationView(); }

		[[nodiscard]] std::expected<void, RegistryDeclarationError> Declare(ResourceLocation key);

		[[nodiscard]] std::expected<StaticRegistry, RegistryBuildError> Build()&&;

	private:
		ResourceLocation m_Key;
		RegistryBuilder<StaticRegistryValue> m_Entries;
	};

	struct StaticRegistryCollectionLimits final {
		std::size_t MaximumRegistries{ 1'024 };
		RegistryLimits Entries{};
		std::size_t MaximumTotalEntries{ 4'194'304 };
	};

	enum class StaticRegistryCollectionDeclarationErrorCode : std::uint8_t {
		RegistryLimitExceeded,
		DuplicateRegistry,
		EntryLimitExceeded,
		TotalEntryLimitExceeded,
	};

	struct StaticRegistryCollectionDeclarationError final {
		StaticRegistryCollectionDeclarationErrorCode Code;
		ResourceLocation RegistryKey;
		std::size_t RegistryIndex{ 0 };
		std::optional<std::size_t> ExistingRegistryIndex{};
		std::size_t RegistryEntryCount{ 0 };
		std::size_t TotalEntryCount{ 0 };
		std::size_t Limit{ 0 };

		auto operator<=>(const StaticRegistryCollectionDeclarationError&) const noexcept = default;
	};

	enum class StaticRegistryCollectionBuildErrorCode : std::uint8_t {
		InvalidGeneration,
	};

	struct StaticRegistryCollectionBuildError final {
		StaticRegistryCollectionBuildErrorCode Code;
		RegistryGeneration Generation{ NoRegistryGeneration };

		auto operator<=>(const StaticRegistryCollectionBuildError&) const noexcept = default;
	};

	class StaticRegistryCollection final {
	public:
		StaticRegistryCollection(const StaticRegistryCollection&) = delete;
		StaticRegistryCollection& operator=(const StaticRegistryCollection&) = delete;
		StaticRegistryCollection(StaticRegistryCollection&&) noexcept = default;
		StaticRegistryCollection& operator=(StaticRegistryCollection&&) noexcept = default;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] bool Empty() const noexcept { return m_Registries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Registries.size(); }
		[[nodiscard]] std::size_t GetTotalEntryCount() const noexcept { return m_TotalEntries; }
		[[nodiscard]] std::span<const StaticRegistry> GetRegistries() const noexcept { return m_Registries; }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_KeyToIndex.contains(key); }

		[[nodiscard]] const StaticRegistry* Find(const ResourceLocation& key) const noexcept;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		StaticRegistryCollection(
			RegistryGeneration generation,
			std::vector<StaticRegistry> registries,
			KeyIndex key_to_index,
			std::size_t total_entries) noexcept;

		RegistryGeneration m_Generation{ NoRegistryGeneration };
		std::vector<StaticRegistry> m_Registries;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalEntries{ 0 };

		friend class StaticRegistryCollectionBuilder;
	};

	class StaticRegistryCollectionBuilder final {
	public:
		StaticRegistryCollectionBuilder() = default;
		explicit StaticRegistryCollectionBuilder(StaticRegistryCollectionLimits limits) noexcept;

		StaticRegistryCollectionBuilder(const StaticRegistryCollectionBuilder&) = delete;
		StaticRegistryCollectionBuilder& operator=(const StaticRegistryCollectionBuilder&) = delete;
		StaticRegistryCollectionBuilder(StaticRegistryCollectionBuilder&&) noexcept = default;
		StaticRegistryCollectionBuilder& operator=(StaticRegistryCollectionBuilder&&) noexcept = default;

		[[nodiscard]] StaticRegistryCollectionLimits GetLimits() const noexcept { return m_Limits; }
		[[nodiscard]] bool Empty() const noexcept { return m_Registries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Registries.size(); }
		[[nodiscard]] std::size_t GetTotalEntryCount() const noexcept { return m_TotalEntries; }
		[[nodiscard]] std::span<const StaticRegistry> GetRegistries() const noexcept { return m_Registries; }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_KeyToIndex.contains(key); }
		[[nodiscard]] const StaticRegistry* Find(const ResourceLocation& key) const noexcept;

		[[nodiscard]] StaticRegistryBuilder CreateRegistryBuilder(ResourceLocation key) const;
		[[nodiscard]] std::expected<void, StaticRegistryCollectionDeclarationError> Declare(StaticRegistry&& registry);
		[[nodiscard]] std::expected<StaticRegistryCollection, StaticRegistryCollectionBuildError> Build(RegistryGeneration generation)&&;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		StaticRegistryCollectionLimits m_Limits;
		std::vector<StaticRegistry> m_Registries;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalEntries{ 0 };
	};

	using StaticRegistryTag = RegistryTag<StaticRegistryValue>;

	class StaticRegistryTagSection final {
	public:
		using Tag = StaticRegistryTag;
		using TagSet = RegistryTagSet<StaticRegistryValue>;

		StaticRegistryTagSection(const StaticRegistryTagSection&) = delete;
		StaticRegistryTagSection& operator=(const StaticRegistryTagSection&) = delete;
		StaticRegistryTagSection(StaticRegistryTagSection&&) noexcept = default;
		StaticRegistryTagSection& operator=(StaticRegistryTagSection&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetRegistryKey() const noexcept { return m_RegistryKey; }
		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Tags.GetGeneration(); }
		[[nodiscard]] bool Empty() const noexcept { return m_Tags.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Tags.Size(); }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const Tag> GetTags() const noexcept { return m_Tags.GetTags(); }
		[[nodiscard]] const Tag* Find(const ResourceLocation& key) const noexcept { return m_Tags.Find(key); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Tags.Contains(key); }

	private:
		StaticRegistryTagSection(ResourceLocation registry_key, TagSet tags, std::size_t total_members) noexcept;

		ResourceLocation m_RegistryKey;
		TagSet m_Tags;
		std::size_t m_TotalMembers{ 0 };

		friend class StaticRegistryTagSectionBuilder;
	};

	class StaticRegistryTagSectionBuilder final {
	public:
		using TagBuilder = RegistryTagBuilder<StaticRegistryValue>;
		using DeclarationView = RegistryTagDeclarationView<StaticRegistryValue>;

		explicit StaticRegistryTagSectionBuilder(ResourceLocation registry_key, RegistryTagLimits limits = {}) noexcept;

		StaticRegistryTagSectionBuilder(const StaticRegistryTagSectionBuilder&) = delete;
		StaticRegistryTagSectionBuilder& operator=(const StaticRegistryTagSectionBuilder&) = delete;
		StaticRegistryTagSectionBuilder(StaticRegistryTagSectionBuilder&&) noexcept = default;
		StaticRegistryTagSectionBuilder& operator=(StaticRegistryTagSectionBuilder&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetRegistryKey() const noexcept { return m_RegistryKey; }
		[[nodiscard]] bool Empty() const noexcept { return m_Tags.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Tags.Size(); }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_Tags.TotalMemberCount(); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Tags.Contains(key); }
		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept { return m_Tags.GetDeclarationView(); }
		[[nodiscard]] std::expected<void, RegistryTagDeclarationError> Declare(ResourceLocation key, std::vector<ResourceLocation> members) {
			return m_Tags.Declare(std::move(key), std::move(members));
		}

		[[nodiscard]] std::expected<void, RegistryTagBuildError> Validate(RegistryGeneration generation, const StaticRegistry& registry) const;
		[[nodiscard]] std::expected<StaticRegistryTagSection, RegistryTagBuildError> Build(RegistryGeneration generation, const StaticRegistry& registry)&&;

	private:
		ResourceLocation m_RegistryKey;
		TagBuilder m_Tags;
	};

	struct StaticRegistryTagCollectionLimits final {
		std::size_t MaximumSections{ 1'024 };
		RegistryTagLimits Tags{};
		std::size_t MaximumTotalTags{ 65'536 };
		std::size_t MaximumTotalMembers{ 4'194'304 };
	};

	enum class StaticRegistryTagCollectionDeclarationErrorCode : std::uint8_t {
		SectionLimitExceeded,
		DuplicateRegistrySection,
		TagLimitExceeded,
		TotalTagLimitExceeded,
		TotalMemberLimitExceeded,
	};

	struct StaticRegistryTagCollectionDeclarationError final {
		StaticRegistryTagCollectionDeclarationErrorCode Code;
		ResourceLocation RegistryKey;
		std::size_t SectionIndex{ 0 };
		std::optional<std::size_t> ExistingSectionIndex{};
		std::size_t SectionTagCount{ 0 };
		std::size_t SectionMemberCount{ 0 };
		std::size_t TotalTagCount{ 0 };
		std::size_t TotalMemberCount{ 0 };
		std::size_t Limit{ 0 };

		auto operator<=>(const StaticRegistryTagCollectionDeclarationError&) const noexcept = default;
	};

	enum class StaticRegistryTagCollectionBuildErrorCode : std::uint8_t {
		InvalidGeneration,
		MissingRegistry,
		MissingMember,
	};

	struct StaticRegistryTagCollectionBuildError final {
		StaticRegistryTagCollectionBuildErrorCode Code;
		RegistryGeneration Generation{ NoRegistryGeneration };
		std::optional<ResourceLocation> RegistryKey{};
		std::size_t SectionIndex{ 0 };
		std::optional<ResourceLocation> TagKey{};
		std::size_t TagIndex{ 0 };
		std::optional<ResourceLocation> MemberKey{};
		std::optional<std::size_t> MemberIndex{};

		auto operator<=>(const StaticRegistryTagCollectionBuildError&) const noexcept = default;
	};

	class StaticRegistryTagCollection final {
	public:
		StaticRegistryTagCollection(const StaticRegistryTagCollection&) = delete;
		StaticRegistryTagCollection& operator=(const StaticRegistryTagCollection&) = delete;
		StaticRegistryTagCollection(StaticRegistryTagCollection&&) noexcept = default;
		StaticRegistryTagCollection& operator=(StaticRegistryTagCollection&&) noexcept = default;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] bool Empty() const noexcept { return m_Sections.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Sections.size(); }
		[[nodiscard]] std::size_t GetTotalTagCount() const noexcept { return m_TotalTags; }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const StaticRegistryTagSection> GetSections() const noexcept { return m_Sections; }
		[[nodiscard]] bool Contains(const ResourceLocation& registry_key) const noexcept { return m_KeyToIndex.contains(registry_key); }
		[[nodiscard]] const StaticRegistryTagSection* Find(const ResourceLocation& registry_key) const noexcept;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		StaticRegistryTagCollection(
			RegistryGeneration generation,
			std::vector<StaticRegistryTagSection> sections,
			KeyIndex key_to_index,
			std::size_t total_tags,
			std::size_t total_members) noexcept;

		RegistryGeneration m_Generation{ NoRegistryGeneration };
		std::vector<StaticRegistryTagSection> m_Sections;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalTags{ 0 };
		std::size_t m_TotalMembers{ 0 };

		friend class StaticRegistryTagCollectionBuilder;
	};

	class StaticRegistryTagCollectionBuilder final {
	public:
		StaticRegistryTagCollectionBuilder() = default;
		explicit StaticRegistryTagCollectionBuilder(StaticRegistryTagCollectionLimits limits) noexcept;

		StaticRegistryTagCollectionBuilder(const StaticRegistryTagCollectionBuilder&) = delete;
		StaticRegistryTagCollectionBuilder& operator=(const StaticRegistryTagCollectionBuilder&) = delete;
		StaticRegistryTagCollectionBuilder(StaticRegistryTagCollectionBuilder&&) noexcept = default;
		StaticRegistryTagCollectionBuilder& operator=(StaticRegistryTagCollectionBuilder&&) noexcept = default;

		[[nodiscard]] bool Empty() const noexcept { return m_Sections.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Sections.size(); }
		[[nodiscard]] std::size_t GetTotalTagCount() const noexcept { return m_TotalTags; }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const StaticRegistryTagSectionBuilder> GetSections() const noexcept { return m_Sections; }
		[[nodiscard]] bool Contains(const ResourceLocation& registry_key) const noexcept { return m_KeyToIndex.contains(registry_key); }
		[[nodiscard]] const StaticRegistryTagSectionBuilder* Find(const ResourceLocation& registry_key) const noexcept;

		[[nodiscard]] StaticRegistryTagSectionBuilder CreateSectionBuilder(ResourceLocation registry_key) const;
		[[nodiscard]] std::expected<void, StaticRegistryTagCollectionDeclarationError> Declare(StaticRegistryTagSectionBuilder&& section);
		[[nodiscard]] std::expected<void, StaticRegistryTagCollectionBuildError> Validate(RegistryGeneration generation, const StaticRegistryCollectionBuilder& registries) const;
		[[nodiscard]] std::expected<void, StaticRegistryTagCollectionBuildError> Validate(RegistryGeneration generation, const StaticRegistryCollection& registries) const;
		[[nodiscard]] std::expected<StaticRegistryTagCollection, StaticRegistryTagCollectionBuildError> Build(RegistryGeneration generation, const StaticRegistryCollection& registries)&&;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		StaticRegistryTagCollectionLimits m_Limits;
		std::vector<StaticRegistryTagSectionBuilder> m_Sections;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalTags{ 0 };
		std::size_t m_TotalMembers{ 0 };
	};
}
