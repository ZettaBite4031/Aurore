#pragma once

#include "Nbt.hpp"
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
#include <vector>

namespace Aurore::Util {
	/*
		A null value represents a Registry Data entry whose payload is
		supplied through a selected Known Pack.
	*/
	using NetworkRegistryValue = std::optional<NbtCompound>;

	class NetworkRegistryBuilder;
	class NetworkRegistryTagSectionBuilder;

	class NetworkRegistry final {
	public:
		using Entry = RegistryEntry<NetworkRegistryValue>;

		NetworkRegistry(const NetworkRegistry&) = delete;
		NetworkRegistry& operator=(const NetworkRegistry&) = delete;
		NetworkRegistry(NetworkRegistry&&) noexcept = default;
		NetworkRegistry& operator=(NetworkRegistry&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetKey() const noexcept { return m_Key; }
		[[nodiscard]] bool Empty() const noexcept { return m_Entries.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.Size(); }
		[[nodiscard]] std::size_t GetEntriesWithDataCount() const noexcept { return m_EntriesWithData; }
		[[nodiscard]] std::span<const Entry> GetEntries() const noexcept { return m_Entries.GetEntries(); }
		[[nodiscard]] const Entry* Find(const ResourceLocation& key) const noexcept { return m_Entries.Find(key); }
		[[nodiscard]] const Entry* Find(RegistryRuntimeId runtime_id) const noexcept { return m_Entries.Find(runtime_id); }
		[[nodiscard]] std::optional<RegistryRuntimeId> FindRuntimeId(const ResourceLocation& key) const noexcept { return m_Entries.FindRuntimeId(key); }
		[[nodiscard]] const ResourceLocation* FindKey(RegistryRuntimeId runtime_id) const noexcept { return m_Entries.FindKey(runtime_id); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Entries.Contains(key); }

	private:
		using EntryRegistry = Registry<NetworkRegistryValue>;

		NetworkRegistry(ResourceLocation key, EntryRegistry entries, std::size_t entries_with_data) noexcept;

		ResourceLocation m_Key;
		EntryRegistry m_Entries;
		std::size_t m_EntriesWithData{ 0 };

		friend class NetworkRegistryBuilder;
		friend class NetworkRegistryTagSectionBuilder;
	};

	class NetworkRegistryBuilder final {
	public:
		using DeclarationView = RegistryDeclarationView<NetworkRegistryValue>;

		explicit NetworkRegistryBuilder(ResourceLocation key, RegistryLimits limits = {}) noexcept;

		NetworkRegistryBuilder(const NetworkRegistryBuilder&) = delete;
		NetworkRegistryBuilder& operator=(const NetworkRegistryBuilder&) = delete;
		NetworkRegistryBuilder(NetworkRegistryBuilder&&) noexcept = default;
		NetworkRegistryBuilder& operator=(NetworkRegistryBuilder&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetKey() const noexcept { return m_Key; }
		[[nodiscard]] RegistryLimits GetLimits() const noexcept { return m_Entries.GetLimits(); }
		[[nodiscard]] bool Empty() const noexcept { return m_Entries.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.Size(); }
		[[nodiscard]] std::size_t GetEntriesWithDataCount() const noexcept { return m_EntriesWithData; }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Entries.Contains(key); }
		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept { return m_Entries.GetDeclarationView(); }

		[[nodiscard]] std::expected<void, RegistryDeclarationError> Declare(ResourceLocation key, NetworkRegistryValue data = std::nullopt);

		[[nodiscard]] std::expected<NetworkRegistry, RegistryBuildError> Build()&&;

	private:
		ResourceLocation m_Key;
		RegistryBuilder<NetworkRegistryValue> m_Entries;
		std::size_t m_EntriesWithData{ 0 };
	};

	struct NetworkRegistryCollectionLimits final {
		std::size_t MaximumRegistries{ 1'024 };
		RegistryLimits Entries{};
		std::size_t MaximumTotalEntries{ 4'194'304 };
	};

	enum class NetworkRegistryCollectionDeclarationErrorCode : std::uint8_t {
		RegistryLimitExceeded,
		DuplicateRegistry,
		EntryLimitExceeded,
		TotalEntryLimitExceeded,
	};

	struct NetworkRegistryCollectionDeclarationError final {
		NetworkRegistryCollectionDeclarationErrorCode Code;
		ResourceLocation RegistryKey;

		std::size_t RegistryIndex{ 0 };
		std::optional<std::size_t> ExistingRegistryIndex{};

		std::size_t RegistryEntryCount{ 0 };
		std::size_t TotalEntryCount{ 0 };
		std::size_t Limit{ 0 };

		auto operator<=>(const NetworkRegistryCollectionDeclarationError&) const noexcept = default;
	};

	enum class NetworkRegistryCollectionBuildErrorCode : std::uint8_t {
		InvalidGeneration,
	};

	struct NetworkRegistryCollectionBuildError final {
		NetworkRegistryCollectionBuildErrorCode Code;
		RegistryGeneration Generation{ NoRegistryGeneration };

		auto operator<=>(const NetworkRegistryCollectionBuildError&) const noexcept = default;
	};

	class NetworkRegistryCollectionBuilder;

	class NetworkRegistryCollection final {
	public:
		NetworkRegistryCollection(const NetworkRegistryCollection&) = delete;
		NetworkRegistryCollection& operator=(const NetworkRegistryCollection&) = delete;
		NetworkRegistryCollection(NetworkRegistryCollection&&) noexcept = default;
		NetworkRegistryCollection& operator=(NetworkRegistryCollection&&) noexcept = default;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] bool Empty() const noexcept { return m_Registries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Registries.size(); }
		[[nodiscard]] std::size_t GetTotalEntryCount() const noexcept { return m_TotalEntries; }
		[[nodiscard]] std::size_t GetEntriesWithDataCount() const noexcept { return m_EntriesWithData; }
		[[nodiscard]] std::span<const NetworkRegistry> GetRegistries() const noexcept { return m_Registries; }

		[[nodiscard]] const NetworkRegistry* Find(const ResourceLocation& key) const noexcept;

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_KeyToIndex.contains(key); }

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		NetworkRegistryCollection(RegistryGeneration generation, std::vector<NetworkRegistry> registries, KeyIndex key_to_index, std::size_t total_entries, std::size_t entries_with_data) noexcept;

		RegistryGeneration m_Generation{ NoRegistryGeneration };
		std::vector<NetworkRegistry> m_Registries;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalEntries{ 0 };
		std::size_t m_EntriesWithData{ 0 };

		friend class NetworkRegistryCollectionBuilder;
	};

	class NetworkRegistryCollectionBuilder final {
	public:
		NetworkRegistryCollectionBuilder() = default;

		explicit NetworkRegistryCollectionBuilder(NetworkRegistryCollectionLimits limits) noexcept;

		NetworkRegistryCollectionBuilder(const NetworkRegistryCollectionBuilder&) = delete;
		NetworkRegistryCollectionBuilder& operator=(const NetworkRegistryCollectionBuilder&) = delete;
		NetworkRegistryCollectionBuilder(NetworkRegistryCollectionBuilder&&) noexcept = default;
		NetworkRegistryCollectionBuilder& operator=(NetworkRegistryCollectionBuilder&&) noexcept = default;

		[[nodiscard]] NetworkRegistryCollectionLimits GetLimits() const noexcept { return m_Limits; }
		[[nodiscard]] bool Empty() const noexcept { return m_Registries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Registries.size(); }
		[[nodiscard]] std::size_t GetTotalEntryCount() const noexcept { return m_TotalEntries; }
		[[nodiscard]] std::size_t GetEntriesWithDataCount() const noexcept { return m_EntriesWithData; }
		[[nodiscard]] std::span<const NetworkRegistry> GetRegistries() const noexcept { return m_Registries; }

		[[nodiscard]] const NetworkRegistry* Find(const ResourceLocation& key) const noexcept;

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_KeyToIndex.contains(key); }

		[[nodiscard]] NetworkRegistryBuilder CreateRegistryBuilder(ResourceLocation key) const;

		/*
			The candidate registry is not moved unless every collection
			constraint has passed.
		*/
		[[nodiscard]] std::expected<void,NetworkRegistryCollectionDeclarationError> Declare(NetworkRegistry&& registry);

		[[nodiscard]] std::expected<NetworkRegistryCollection, NetworkRegistryCollectionBuildError> Build(RegistryGeneration generation)&&;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		NetworkRegistryCollectionLimits m_Limits;
		std::vector<NetworkRegistry> m_Registries;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalEntries{ 0 };
		std::size_t m_EntriesWithData{ 0 };
	};

	using NetworkRegistryTag = RegistryTag<NetworkRegistryValue>;

	class NetworkRegistryTagSection final {
	public:
		using Tag = NetworkRegistryTag;
		using TagSet = RegistryTagSet<NetworkRegistryValue>;

		NetworkRegistryTagSection(const NetworkRegistryTagSection&) = delete;
		NetworkRegistryTagSection& operator=(const NetworkRegistryTagSection&) = delete;
		NetworkRegistryTagSection(NetworkRegistryTagSection&&) noexcept = default;
		NetworkRegistryTagSection& operator=(NetworkRegistryTagSection&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetRegistryKey() const noexcept { return m_RegistryKey; }
		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Tags.GetGeneration(); }
		[[nodiscard]] bool Empty() const noexcept { return m_Tags.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Tags.Size(); }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const Tag> GetTags() const noexcept { return m_Tags.GetTags(); }
		[[nodiscard]] const Tag* Find(const ResourceLocation& key) const noexcept { return m_Tags.Find(key); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Tags.Contains(key); }

	private:
		NetworkRegistryTagSection(ResourceLocation registry_key, TagSet tags, std::size_t total_members) noexcept;

		ResourceLocation m_RegistryKey;
		TagSet m_Tags;
		std::size_t m_TotalMembers{ 0 };

		friend class NetworkRegistryTagSectionBuilder;
	};

	class NetworkRegistryTagSectionBuilder final {
	public:
		using TagBuilder = RegistryTagBuilder<NetworkRegistryValue>;
		using DeclarationView = RegistryTagDeclarationView<NetworkRegistryValue>;

		explicit NetworkRegistryTagSectionBuilder(ResourceLocation registry_key, RegistryTagLimits limits = {}) noexcept;

		NetworkRegistryTagSectionBuilder(const NetworkRegistryTagSectionBuilder&) = delete;
		NetworkRegistryTagSectionBuilder& operator=(const NetworkRegistryTagSectionBuilder&) = delete;
		NetworkRegistryTagSectionBuilder(NetworkRegistryTagSectionBuilder&&) noexcept = default;
		NetworkRegistryTagSectionBuilder& operator=(NetworkRegistryTagSectionBuilder&&) noexcept = default;

		[[nodiscard]] const ResourceLocation& GetRegistryKey() const noexcept { return m_RegistryKey; }
		[[nodiscard]] RegistryTagLimits GetLimits() const noexcept { return m_Tags.GetLimits(); }
		[[nodiscard]] bool Empty() const noexcept { return m_Tags.Empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Tags.Size(); }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_Tags.TotalMemberCount(); }
		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept { return m_Tags.Contains(key); }
		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept { return m_Tags.GetDeclarationView(); }
		[[nodiscard]] std::expected<void, RegistryTagDeclarationError> Declare(ResourceLocation key, std::vector<ResourceLocation> members) {
			return m_Tags.Declare(std::move(key), std::move(members));
		}

		[[nodiscard]] std::expected<void, RegistryTagBuildError> Validate(RegistryGeneration generation, const NetworkRegistry& registry) const;

		[[nodiscard]] std::expected<NetworkRegistryTagSection, RegistryTagBuildError> Build(RegistryGeneration generation, const NetworkRegistry& registry)&&;

	private:
		ResourceLocation m_RegistryKey;
		TagBuilder m_Tags;
	};

	struct NetworkRegistryTagCollectionLimits final {
		std::size_t MaximumSections{ 1'024 };
		RegistryTagLimits Tags{};
		std::size_t MaximumTotalTags{ 65'536 };
		std::size_t MaximumTotalMembers{ 4'194'304 };
	};

	enum class NetworkRegistryTagCollectionDeclarationErrorCode : std::uint8_t {
		SectionLimitExceeded,
		DuplicateRegistrySection,
		TagLimitExceeded,
		TotalTagLimitExceeded,
		TotalMemberLimitExceeded,
	};

	struct NetworkRegistryTagCollectionDeclarationError final {
		NetworkRegistryTagCollectionDeclarationErrorCode Code;
		ResourceLocation RegistryKey;

		std::size_t SectionIndex{ 0 };
		std::optional<std::size_t> ExistingSectionIndex{};

		std::size_t SectionTagCount{ 0 };
		std::size_t SectionMemberCount{ 0 };
		std::size_t TotalTagCount{ 0 };
		std::size_t TotalMemberCount{ 0 };
		std::size_t Limit{ 0 };

		auto operator<=>(const NetworkRegistryTagCollectionDeclarationError&) const noexcept = default;
	};

	enum class NetworkRegistryTagCollectionBuildErrorCode : std::uint8_t {
		InvalidGeneration,
		MissingRegistry,
		MissingMember,
	};

	struct NetworkRegistryTagCollectionBuildError final {
		NetworkRegistryTagCollectionBuildErrorCode Code;
		RegistryGeneration Generation{ NoRegistryGeneration };

		std::optional<ResourceLocation> RegistryKey{};
		std::size_t SectionIndex{ 0 };

		std::optional<ResourceLocation> TagKey{};
		std::size_t TagIndex{ 0 };

		std::optional<ResourceLocation> MemberKey{};
		std::optional<std::size_t> MemberIndex{};

		auto operator<=>(const NetworkRegistryTagCollectionBuildError&) const noexcept = default;
	};

	class NetworkRegistryTagCollectionBuilder;

	class NetworkRegistryTagCollection final {
	public:
		NetworkRegistryTagCollection(const NetworkRegistryTagCollection&) = delete;
		NetworkRegistryTagCollection& operator=(const NetworkRegistryTagCollection&) = delete;
		NetworkRegistryTagCollection(NetworkRegistryTagCollection&&) noexcept = default;
		NetworkRegistryTagCollection& operator=(NetworkRegistryTagCollection&&) noexcept = default;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] bool Empty() const noexcept { return m_Sections.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Sections.size(); }
		[[nodiscard]] std::size_t GetTotalTagCount() const noexcept { return m_TotalTags; }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const NetworkRegistryTagSection> GetSections() const noexcept { return m_Sections; }
		[[nodiscard]] bool Contains(const ResourceLocation& registry_key) const noexcept { return m_KeyToIndex.contains(registry_key); }

		[[nodiscard]] const NetworkRegistryTagSection* Find(const ResourceLocation& registry_key) const noexcept;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		NetworkRegistryTagCollection(RegistryGeneration generation, std::vector<NetworkRegistryTagSection> sections, KeyIndex key_to_index, std::size_t total_tags, std::size_t total_members) noexcept;

		RegistryGeneration m_Generation{ NoRegistryGeneration };

		std::vector<NetworkRegistryTagSection> m_Sections;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalTags{ 0 };
		std::size_t m_TotalMembers{ 0 };

		friend class NetworkRegistryTagCollectionBuilder;
	};

	class NetworkRegistryTagCollectionBuilder final {
	public:
		NetworkRegistryTagCollectionBuilder() = default;

		explicit NetworkRegistryTagCollectionBuilder(NetworkRegistryTagCollectionLimits limits) noexcept;

		NetworkRegistryTagCollectionBuilder(const NetworkRegistryTagCollectionBuilder&) = delete;
		NetworkRegistryTagCollectionBuilder& operator=(const NetworkRegistryTagCollectionBuilder&) = delete;
		NetworkRegistryTagCollectionBuilder(NetworkRegistryTagCollectionBuilder&&) noexcept = default;
		NetworkRegistryTagCollectionBuilder& operator=(NetworkRegistryTagCollectionBuilder&&) noexcept = default;

		[[nodiscard]] NetworkRegistryTagCollectionLimits GetLimits() const noexcept { return m_Limits; }
		[[nodiscard]] bool Empty() const noexcept { return m_Sections.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Sections.size(); }
		[[nodiscard]] std::size_t GetTotalTagCount() const noexcept { return m_TotalTags; }
		[[nodiscard]] std::size_t GetTotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] std::span<const NetworkRegistryTagSectionBuilder> GetSections() const noexcept { return m_Sections; }
		[[nodiscard]] bool Contains(const ResourceLocation& registry_key) const noexcept { return m_KeyToIndex.contains(registry_key); }

		[[nodiscard]] const NetworkRegistryTagSectionBuilder* Find(const ResourceLocation& registry_key) const noexcept;

		[[nodiscard]] NetworkRegistryTagSectionBuilder CreateSectionBuilder(ResourceLocation registry_key) const;

		[[nodiscard]] std::expected<void, NetworkRegistryTagCollectionDeclarationError> Declare(NetworkRegistryTagSectionBuilder&& section);

		[[nodiscard]] std::expected<void, NetworkRegistryTagCollectionBuildError> Validate(RegistryGeneration generation, const NetworkRegistryCollectionBuilder& registries) const;
		[[nodiscard]] std::expected<void, NetworkRegistryTagCollectionBuildError> Validate(RegistryGeneration generation, const NetworkRegistryCollection& registries) const;

		[[nodiscard]] std::expected<NetworkRegistryTagCollection, NetworkRegistryTagCollectionBuildError> Build(RegistryGeneration generation, const NetworkRegistryCollection& registries)&&;

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		NetworkRegistryTagCollectionLimits m_Limits;
		std::vector<NetworkRegistryTagSectionBuilder> m_Sections;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalTags{ 0 };
		std::size_t m_TotalMembers{ 0 };
	};
}
