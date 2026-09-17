#include <Aurore/Util/NetworkRegistry.hpp>

#include <utility>

namespace {
	[[nodiscard]] Aurore::Util::NetworkRegistryTagCollectionBuildError TranslateTagBuildError(
		Aurore::Util::RegistryGeneration generation, const Aurore::Util::ResourceLocation& registry_key,
		std::size_t section_index, const Aurore::Util::RegistryTagBuildError& error) {

		using Aurore::Util::NetworkRegistryTagCollectionBuildError;
		using Aurore::Util::NetworkRegistryTagCollectionBuildErrorCode;
		using Aurore::Util::RegistryTagBuildErrorCode;

		return NetworkRegistryTagCollectionBuildError{
			.Code = error.Code == RegistryTagBuildErrorCode::InvalidGeneration
				? NetworkRegistryTagCollectionBuildErrorCode::InvalidGeneration
				: NetworkRegistryTagCollectionBuildErrorCode::MissingMember,
			.Generation = generation,
			.RegistryKey = registry_key,
			.SectionIndex = section_index,
			.TagKey = error.TagKey,
			.TagIndex = error.TagIndex,
			.MemberKey = error.MemberKey,
			.MemberIndex = error.MemberIndex,
		};
	}
}

namespace Aurore::Util {
	NetworkRegistry::NetworkRegistry(ResourceLocation key, EntryRegistry entries, std::size_t entries_with_data) noexcept
		: m_Key(std::move(key)), m_Entries(std::move(entries)), m_EntriesWithData(entries_with_data) {}

	NetworkRegistryBuilder::NetworkRegistryBuilder(ResourceLocation key, RegistryLimits limits) noexcept
		: m_Key(std::move(key)), m_Entries(limits) {}

	std::expected<void, RegistryDeclarationError> NetworkRegistryBuilder::Declare(ResourceLocation key, NetworkRegistryValue data) {
		const bool has_data = data.has_value();
		auto result = m_Entries.Declare(std::move(key), std::move(data));
		if (!result) return std::unexpected(result.error());
		if (has_data) m_EntriesWithData++;
		return {};
	}

	std::expected<NetworkRegistry, RegistryBuildError> NetworkRegistryBuilder::Build()&& {
		auto entries = std::move(m_Entries).Build();
		if (!entries) return std::unexpected(entries.error());
		return NetworkRegistry{
			std::move(m_Key),
			std::move(*entries),
			m_EntriesWithData
		};
	}

	NetworkRegistryCollection::NetworkRegistryCollection(RegistryGeneration generation, std::vector<NetworkRegistry> registries, KeyIndex key_to_index, std::size_t total_entries, std::size_t entries_with_data) noexcept
		: m_Generation(generation), m_Registries(std::move(registries)), m_KeyToIndex(std::move(key_to_index)), m_TotalEntries(total_entries), m_EntriesWithData(entries_with_data) {}

	const NetworkRegistry* NetworkRegistryCollection::Find(const ResourceLocation& key) const noexcept {
		const auto iterator = m_KeyToIndex.find(key);
		if (iterator == m_KeyToIndex.end()) return nullptr;
		return &m_Registries[iterator->second];
	}

	NetworkRegistryCollectionBuilder::NetworkRegistryCollectionBuilder(NetworkRegistryCollectionLimits limits) noexcept
		: m_Limits(limits) {}

	const NetworkRegistry* NetworkRegistryCollectionBuilder::Find(const ResourceLocation& key) const noexcept {
		const auto iterator = m_KeyToIndex.find(key);
		if (iterator == m_KeyToIndex.end()) return nullptr;
		return &m_Registries[iterator->second];
	}

	NetworkRegistryBuilder NetworkRegistryCollectionBuilder::CreateRegistryBuilder(ResourceLocation key) const {
		return NetworkRegistryBuilder{
			std::move(key),
			m_Limits.Entries,
		};
	}

	std::expected<void, NetworkRegistryCollectionDeclarationError> NetworkRegistryCollectionBuilder::Declare(NetworkRegistry&& registry) {
		const auto registry_index = m_Registries.size();
		const auto& registry_key = registry.GetKey();
		const auto registry_entry_count = registry.Size();
		const auto registry_entries_with_data = registry.GetEntriesWithDataCount();

		if (registry_index >= m_Limits.MaximumRegistries)
			return std::unexpected(NetworkRegistryCollectionDeclarationError{
				.Code = NetworkRegistryCollectionDeclarationErrorCode::RegistryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = std::nullopt,
				.RegistryEntryCount = registry_entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.MaximumRegistries,
			});


		const auto existing = m_KeyToIndex.find(registry_key);
		if (existing != m_KeyToIndex.end())
			return std::unexpected(NetworkRegistryCollectionDeclarationError{
				.Code = NetworkRegistryCollectionDeclarationErrorCode::DuplicateRegistry,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = existing->second,
				.RegistryEntryCount = registry_entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = 0,
			});


		if (registry_entry_count > m_Limits.Entries.MaximumEntries)
			return std::unexpected(NetworkRegistryCollectionDeclarationError{
				.Code = NetworkRegistryCollectionDeclarationErrorCode::EntryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = std::nullopt,
				.RegistryEntryCount = registry_entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.Entries.MaximumEntries,
			});


		if (m_TotalEntries > m_Limits.MaximumTotalEntries || registry_entry_count > m_Limits.MaximumTotalEntries - m_TotalEntries)
			return std::unexpected(NetworkRegistryCollectionDeclarationError{
				.Code = NetworkRegistryCollectionDeclarationErrorCode::TotalEntryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = std::nullopt,
				.RegistryEntryCount = registry_entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.MaximumTotalEntries,
			});


		const auto [iterator, inserted] = m_KeyToIndex.emplace(registry_key, registry_index);
		if (!inserted)
			return std::unexpected(NetworkRegistryCollectionDeclarationError{
				.Code = NetworkRegistryCollectionDeclarationErrorCode::DuplicateRegistry,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = iterator->second,
				.RegistryEntryCount = registry_entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = 0,
			});

		try {
			m_Registries.push_back(std::move(registry));
		}
		catch (...) {
			m_KeyToIndex.erase(iterator);
			throw;
		}

		m_TotalEntries += registry_entry_count;
		m_EntriesWithData += registry_entries_with_data;

		return {};
	}

	std::expected<NetworkRegistryCollection, NetworkRegistryCollectionBuildError> NetworkRegistryCollectionBuilder::Build(RegistryGeneration generation)&& {
		if (generation == NoRegistryGeneration)
			return std::unexpected(NetworkRegistryCollectionBuildError{
				.Code = NetworkRegistryCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});

		return NetworkRegistryCollection{
			generation,
			std::move(m_Registries),
			std::move(m_KeyToIndex),
			m_TotalEntries,
			m_EntriesWithData,
		};
	}

	NetworkRegistryTagSection::NetworkRegistryTagSection(ResourceLocation registry_key, TagSet tags, std::size_t total_members) noexcept
		: m_RegistryKey(std::move(registry_key)), m_Tags(std::move(tags)), m_TotalMembers(total_members) {}

	NetworkRegistryTagSectionBuilder::NetworkRegistryTagSectionBuilder(ResourceLocation registry_key, RegistryTagLimits limits) noexcept
		: m_RegistryKey(std::move(registry_key)), m_Tags(limits) {}

	std::expected<void, RegistryTagBuildError> NetworkRegistryTagSectionBuilder::Validate(RegistryGeneration generation, const NetworkRegistry& registry) const {
		return m_Tags.Validate(generation, registry.m_Entries);
	}

	std::expected<NetworkRegistryTagSection, RegistryTagBuildError> NetworkRegistryTagSectionBuilder::Build(RegistryGeneration generation, const NetworkRegistry& registry)&& {
		const auto total_members = m_Tags.TotalMemberCount();
		auto tags = std::move(m_Tags).Build(generation, registry.m_Entries);
		if (!tags) return std::unexpected(tags.error());
		return NetworkRegistryTagSection{
			std::move(m_RegistryKey),
			std::move(*tags),
			total_members,
		};
	}

	NetworkRegistryTagCollection::NetworkRegistryTagCollection(RegistryGeneration generation, std::vector<NetworkRegistryTagSection> sections, KeyIndex key_to_index, std::size_t total_tags, std::size_t total_members) noexcept
		: m_Generation(generation), m_Sections(std::move(sections)), m_KeyToIndex(std::move(key_to_index)), m_TotalTags(total_tags), m_TotalMembers(total_members) {}

	const NetworkRegistryTagSection* NetworkRegistryTagCollection::Find(const ResourceLocation& registry_key) const noexcept {
		const auto iterator = m_KeyToIndex.find(registry_key);
		if (iterator == m_KeyToIndex.end()) return nullptr;
		return &m_Sections[iterator->second];
	}

	NetworkRegistryTagCollectionBuilder::NetworkRegistryTagCollectionBuilder(NetworkRegistryTagCollectionLimits limits) noexcept
		: m_Limits(limits) {}

	const NetworkRegistryTagSectionBuilder* NetworkRegistryTagCollectionBuilder::Find(const ResourceLocation& registry_key) const noexcept {
		const auto iterator = m_KeyToIndex.find(registry_key);
		if (iterator == m_KeyToIndex.end()) return nullptr;
		return &m_Sections[iterator->second];
	}

	NetworkRegistryTagSectionBuilder NetworkRegistryTagCollectionBuilder::CreateSectionBuilder(ResourceLocation registry_key) const {
		return NetworkRegistryTagSectionBuilder{
			std::move(registry_key),
			m_Limits.Tags,
		};
	}

	std::expected<void, NetworkRegistryTagCollectionDeclarationError> NetworkRegistryTagCollectionBuilder::Declare(NetworkRegistryTagSectionBuilder&& section) {
		const auto section_index = m_Sections.size();
		const auto& registry_key = section.GetRegistryKey();
		const auto section_tag_count = section.Size();
		const auto section_member_count = section.GetTotalMemberCount();
		if (section_index >= m_Limits.MaximumSections)
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::SectionLimitExceeded,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex = std::nullopt,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = m_Limits.MaximumSections,
			});

		const auto existing = m_KeyToIndex.find(registry_key);
		if (existing != m_KeyToIndex.end())
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::DuplicateRegistrySection,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex =
					existing->second,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = 0,
			});

		if (section_tag_count > m_Limits.Tags.MaximumTags)
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::TagLimitExceeded,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex = std::nullopt,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit =
					m_Limits.Tags.MaximumTags,
			});

		if (m_TotalTags > m_Limits.MaximumTotalTags || section_tag_count > m_Limits.MaximumTotalTags - m_TotalTags)
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::TotalTagLimitExceeded,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex = std::nullopt,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit =
					m_Limits.MaximumTotalTags,
			});


		if (m_TotalMembers > m_Limits.MaximumTotalMembers || section_member_count > m_Limits.MaximumTotalMembers - m_TotalMembers)
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::TotalMemberLimitExceeded,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex = std::nullopt,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit =
					m_Limits.MaximumTotalMembers,
			});


		const auto [iterator, inserted] = m_KeyToIndex.emplace(registry_key, section_index);
		if (!inserted)
			return std::unexpected(NetworkRegistryTagCollectionDeclarationError{
				.Code = NetworkRegistryTagCollectionDeclarationErrorCode::DuplicateRegistrySection,
				.RegistryKey = registry_key,
				.SectionIndex = section_index,
				.ExistingSectionIndex =
					iterator->second,
				.SectionTagCount = section_tag_count,
				.SectionMemberCount =
					section_member_count,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = 0,
			});

		try {
			m_Sections.push_back(std::move(section));
		}
		catch (...) {
			m_KeyToIndex.erase(iterator);
			throw;
		}

		m_TotalTags += section_tag_count;
		m_TotalMembers += section_member_count;

		return {};
	}

	std::expected<void, NetworkRegistryTagCollectionBuildError> NetworkRegistryTagCollectionBuilder::Validate(RegistryGeneration generation, const NetworkRegistryCollectionBuilder& registries) const {
		if (generation == NoRegistryGeneration)
			return std::unexpected(NetworkRegistryTagCollectionBuildError{
				.Code = NetworkRegistryTagCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});


		for (std::size_t section_index{ 0 }; section_index < m_Sections.size(); ++section_index) {
			const auto& section = m_Sections[section_index];
			const auto* registry = registries.Find(section.GetRegistryKey());
			if (registry == nullptr)
				return std::unexpected(NetworkRegistryTagCollectionBuildError{
					.Code = NetworkRegistryTagCollectionBuildErrorCode::MissingRegistry,
					.Generation = generation,
					.RegistryKey = section.GetRegistryKey(),
					.SectionIndex = section_index,
				});

			const auto validation = section.Validate(generation, *registry);
			if (!validation)
				return std::unexpected(TranslateTagBuildError(generation, section.GetRegistryKey(), section_index, validation.error()));
		}

		return {};
	}

	std::expected<void, NetworkRegistryTagCollectionBuildError> NetworkRegistryTagCollectionBuilder::Validate(RegistryGeneration generation, const NetworkRegistryCollection& registries) const {
		if (generation == NoRegistryGeneration)
			return std::unexpected(NetworkRegistryTagCollectionBuildError{
				.Code = NetworkRegistryTagCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});


		for (std::size_t section_index{ 0 }; section_index < m_Sections.size(); ++section_index) {
			const auto& section = m_Sections[section_index];
			const auto* registry = registries.Find(section.GetRegistryKey());
			if (registry == nullptr)
				return std::unexpected(NetworkRegistryTagCollectionBuildError{
					.Code = NetworkRegistryTagCollectionBuildErrorCode::MissingRegistry,
					.Generation = generation,
					.RegistryKey = section.GetRegistryKey(),
					.SectionIndex = section_index,
				});


			const auto validation = section.Validate(generation, *registry);
			if (!validation)
				return std::unexpected(TranslateTagBuildError(generation, section.GetRegistryKey(), section_index, validation.error()));
		}

		return {};
	}

	std::expected<NetworkRegistryTagCollection, NetworkRegistryTagCollectionBuildError> NetworkRegistryTagCollectionBuilder::Build(RegistryGeneration generation, const NetworkRegistryCollection& registries)&& {
		const auto validation = Validate(generation, registries);
		if (!validation) return std::unexpected(validation.error());

		std::vector<NetworkRegistryTagSection> sections;
		sections.reserve(m_Sections.size());

		for (std::size_t section_index{ 0 }; section_index < m_Sections.size(); ++section_index) {
			auto& section = m_Sections[section_index];
			const auto* registry = registries.Find(section.GetRegistryKey());
			auto result = std::move(section).Build(generation, *registry);
			if (!result)
				return std::unexpected(TranslateTagBuildError(generation, section.GetRegistryKey(), section_index, result.error()));
			sections.push_back(std::move(*result));
		}

		return NetworkRegistryTagCollection{
			generation,
			std::move(sections),
			std::move(m_KeyToIndex),
			m_TotalTags,
			m_TotalMembers,
		};
	}
}
