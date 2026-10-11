#include <Aurore/Util/StaticRegistry.hpp>

#include <utility>

namespace {
	[[nodiscard]] Aurore::Util::StaticRegistryTagCollectionBuildError TranslateTagBuildError(
		Aurore::Util::RegistryGeneration generation,
		const Aurore::Util::ResourceLocation& registry_key,
		std::size_t section_index,
		const Aurore::Util::RegistryTagBuildError& error) {

		using Aurore::Util::RegistryTagBuildErrorCode;
		using Aurore::Util::StaticRegistryTagCollectionBuildError;
		using Aurore::Util::StaticRegistryTagCollectionBuildErrorCode;

		return StaticRegistryTagCollectionBuildError{
			.Code = error.Code == RegistryTagBuildErrorCode::InvalidGeneration
				? StaticRegistryTagCollectionBuildErrorCode::InvalidGeneration
				: StaticRegistryTagCollectionBuildErrorCode::MissingMember,
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
	StaticRegistry::StaticRegistry(ResourceLocation key, EntryRegistry entries) noexcept
		: m_Key(std::move(key)), m_Entries(std::move(entries)) {}

	StaticRegistryBuilder::StaticRegistryBuilder(ResourceLocation key, RegistryLimits limits) noexcept
		: m_Key(std::move(key)), m_Entries(limits) {}

	std::expected<void, RegistryDeclarationError> StaticRegistryBuilder::Declare(ResourceLocation key) {
		return m_Entries.Declare(std::move(key), StaticRegistryValue{});
	}

	std::expected<StaticRegistry, RegistryBuildError> StaticRegistryBuilder::Build()&& {
		auto entries = std::move(m_Entries).Build();
		if (!entries) return std::unexpected(entries.error());

		return StaticRegistry{
			std::move(m_Key),
			std::move(*entries),
		};
	}

	StaticRegistryCollection::StaticRegistryCollection(
		RegistryGeneration generation,
		std::vector<StaticRegistry> registries,
		KeyIndex key_to_index,
		std::size_t total_entries) noexcept
		: m_Generation(generation),
		m_Registries(std::move(registries)),
		m_KeyToIndex(std::move(key_to_index)),
		m_TotalEntries(total_entries) {}

	const StaticRegistry* StaticRegistryCollection::Find(const ResourceLocation& key) const noexcept {
		const auto iterator = m_KeyToIndex.find(key);
		return iterator == m_KeyToIndex.end() ? nullptr : &m_Registries[iterator->second];
	}

	StaticRegistryCollectionBuilder::StaticRegistryCollectionBuilder(StaticRegistryCollectionLimits limits) noexcept
		: m_Limits(limits) {}

	const StaticRegistry* StaticRegistryCollectionBuilder::Find(const ResourceLocation& key) const noexcept {
		const auto iterator = m_KeyToIndex.find(key);
		return iterator == m_KeyToIndex.end() ? nullptr : &m_Registries[iterator->second];
	}

	StaticRegistryBuilder StaticRegistryCollectionBuilder::CreateRegistryBuilder(ResourceLocation key) const {
		return StaticRegistryBuilder{ std::move(key), m_Limits.Entries };
	}

	std::expected<void, StaticRegistryCollectionDeclarationError>
	StaticRegistryCollectionBuilder::Declare(StaticRegistry&& registry) {
		const auto registry_index = m_Registries.size();
		const auto& registry_key = registry.GetKey();
		const auto entry_count = registry.Size();

		if (registry_index >= m_Limits.MaximumRegistries)
			return std::unexpected(StaticRegistryCollectionDeclarationError{
				.Code = StaticRegistryCollectionDeclarationErrorCode::RegistryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.RegistryEntryCount = entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.MaximumRegistries,
			});

		const auto existing = m_KeyToIndex.find(registry_key);
		if (existing != m_KeyToIndex.end())
			return std::unexpected(StaticRegistryCollectionDeclarationError{
				.Code = StaticRegistryCollectionDeclarationErrorCode::DuplicateRegistry,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = existing->second,
				.RegistryEntryCount = entry_count,
				.TotalEntryCount = m_TotalEntries,
			});

		if (entry_count > m_Limits.Entries.MaximumEntries)
			return std::unexpected(StaticRegistryCollectionDeclarationError{
				.Code = StaticRegistryCollectionDeclarationErrorCode::EntryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.RegistryEntryCount = entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.Entries.MaximumEntries,
			});

		if (m_TotalEntries > m_Limits.MaximumTotalEntries
			|| entry_count > m_Limits.MaximumTotalEntries - m_TotalEntries)
			return std::unexpected(StaticRegistryCollectionDeclarationError{
				.Code = StaticRegistryCollectionDeclarationErrorCode::TotalEntryLimitExceeded,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.RegistryEntryCount = entry_count,
				.TotalEntryCount = m_TotalEntries,
				.Limit = m_Limits.MaximumTotalEntries,
			});

		const auto [iterator, inserted] = m_KeyToIndex.emplace(registry_key, registry_index);
		if (!inserted)
			return std::unexpected(StaticRegistryCollectionDeclarationError{
				.Code = StaticRegistryCollectionDeclarationErrorCode::DuplicateRegistry,
				.RegistryKey = registry_key,
				.RegistryIndex = registry_index,
				.ExistingRegistryIndex = iterator->second,
				.RegistryEntryCount = entry_count,
				.TotalEntryCount = m_TotalEntries,
			});

		try {
			m_Registries.push_back(std::move(registry));
		}
		catch (...) {
			m_KeyToIndex.erase(iterator);
			throw;
		}

		m_TotalEntries += entry_count;
		return {};
	}

	std::expected<StaticRegistryCollection, StaticRegistryCollectionBuildError>
	StaticRegistryCollectionBuilder::Build(RegistryGeneration generation)&& {
		if (generation == NoRegistryGeneration)
			return std::unexpected(StaticRegistryCollectionBuildError{
				.Code = StaticRegistryCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});

		return StaticRegistryCollection{
			generation,
			std::move(m_Registries),
			std::move(m_KeyToIndex),
			m_TotalEntries,
		};
	}

	StaticRegistryTagSection::StaticRegistryTagSection(
		ResourceLocation registry_key,
		TagSet tags,
		std::size_t total_members) noexcept
		: m_RegistryKey(std::move(registry_key)),
		m_Tags(std::move(tags)),
		m_TotalMembers(total_members) {}

	StaticRegistryTagSectionBuilder::StaticRegistryTagSectionBuilder(
		ResourceLocation registry_key,
		RegistryTagLimits limits) noexcept
		: m_RegistryKey(std::move(registry_key)),
		m_Tags(limits) {}

	std::expected<void, RegistryTagBuildError>
	StaticRegistryTagSectionBuilder::Validate(
		RegistryGeneration generation,
		const StaticRegistry& registry) const {
		return m_Tags.Validate(generation, registry.m_Entries);
	}

	std::expected<StaticRegistryTagSection, RegistryTagBuildError>
	StaticRegistryTagSectionBuilder::Build(
		RegistryGeneration generation,
		const StaticRegistry& registry)&& {
		const auto total_members = m_Tags.TotalMemberCount();
		auto tags = std::move(m_Tags).Build(generation, registry.m_Entries);
		if (!tags) return std::unexpected(tags.error());

		return StaticRegistryTagSection{
			std::move(m_RegistryKey),
			std::move(*tags),
			total_members,
		};
	}

	StaticRegistryTagCollection::StaticRegistryTagCollection(
		RegistryGeneration generation,
		std::vector<StaticRegistryTagSection> sections,
		KeyIndex key_to_index,
		std::size_t total_tags,
		std::size_t total_members) noexcept
		: m_Generation(generation),
		m_Sections(std::move(sections)),
		m_KeyToIndex(std::move(key_to_index)),
		m_TotalTags(total_tags),
		m_TotalMembers(total_members) {}

	const StaticRegistryTagSection* StaticRegistryTagCollection::Find(const ResourceLocation& registry_key) const noexcept {
		const auto iterator = m_KeyToIndex.find(registry_key);
		return iterator == m_KeyToIndex.end() ? nullptr : &m_Sections[iterator->second];
	}

	StaticRegistryTagCollectionBuilder::StaticRegistryTagCollectionBuilder(
		StaticRegistryTagCollectionLimits limits) noexcept
		: m_Limits(limits) {}

	const StaticRegistryTagSectionBuilder* StaticRegistryTagCollectionBuilder::Find(
		const ResourceLocation& registry_key) const noexcept {
		const auto iterator = m_KeyToIndex.find(registry_key);
		return iterator == m_KeyToIndex.end() ? nullptr : &m_Sections[iterator->second];
	}

	StaticRegistryTagSectionBuilder StaticRegistryTagCollectionBuilder::CreateSectionBuilder(
		ResourceLocation registry_key) const {
		return StaticRegistryTagSectionBuilder{ std::move(registry_key), m_Limits.Tags };
	}

	std::expected<void, StaticRegistryTagCollectionDeclarationError>
	StaticRegistryTagCollectionBuilder::Declare(StaticRegistryTagSectionBuilder&& section) {
		const auto index = m_Sections.size();
		const auto& key = section.GetRegistryKey();
		const auto tags = section.Size();
		const auto members = section.GetTotalMemberCount();

		if (index >= m_Limits.MaximumSections)
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::SectionLimitExceeded,
				.RegistryKey = key,
				.SectionIndex = index,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = m_Limits.MaximumSections,
			});

		const auto existing = m_KeyToIndex.find(key);
		if (existing != m_KeyToIndex.end())
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::DuplicateRegistrySection,
				.RegistryKey = key,
				.SectionIndex = index,
				.ExistingSectionIndex = existing->second,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
			});

		if (tags > m_Limits.Tags.MaximumTags)
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::TagLimitExceeded,
				.RegistryKey = key,
				.SectionIndex = index,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = m_Limits.Tags.MaximumTags,
			});

		if (m_TotalTags > m_Limits.MaximumTotalTags
			|| tags > m_Limits.MaximumTotalTags - m_TotalTags)
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::TotalTagLimitExceeded,
				.RegistryKey = key,
				.SectionIndex = index,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = m_Limits.MaximumTotalTags,
			});

		if (m_TotalMembers > m_Limits.MaximumTotalMembers
			|| members > m_Limits.MaximumTotalMembers - m_TotalMembers)
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::TotalMemberLimitExceeded,
				.RegistryKey = key,
				.SectionIndex = index,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
				.Limit = m_Limits.MaximumTotalMembers,
			});

		const auto [iterator, inserted] = m_KeyToIndex.emplace(key, index);
		if (!inserted)
			return std::unexpected(StaticRegistryTagCollectionDeclarationError{
				.Code = StaticRegistryTagCollectionDeclarationErrorCode::DuplicateRegistrySection,
				.RegistryKey = key,
				.SectionIndex = index,
				.ExistingSectionIndex = iterator->second,
				.SectionTagCount = tags,
				.SectionMemberCount = members,
				.TotalTagCount = m_TotalTags,
				.TotalMemberCount = m_TotalMembers,
			});

		try {
			m_Sections.push_back(std::move(section));
		}
		catch (...) {
			m_KeyToIndex.erase(iterator);
			throw;
		}

		m_TotalTags += tags;
		m_TotalMembers += members;
		return {};
	}

	std::expected<void, StaticRegistryTagCollectionBuildError>
	StaticRegistryTagCollectionBuilder::Validate(
		RegistryGeneration generation,
		const StaticRegistryCollectionBuilder& registries) const {
		if (generation == NoRegistryGeneration)
			return std::unexpected(StaticRegistryTagCollectionBuildError{
				.Code = StaticRegistryTagCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});

		for (std::size_t index{ 0 }; index < m_Sections.size(); ++index) {
			const auto& section = m_Sections[index];
			const auto* registry = registries.Find(section.GetRegistryKey());
			if (registry == nullptr)
				return std::unexpected(StaticRegistryTagCollectionBuildError{
					.Code = StaticRegistryTagCollectionBuildErrorCode::MissingRegistry,
					.Generation = generation,
					.RegistryKey = section.GetRegistryKey(),
					.SectionIndex = index,
				});

			const auto result = section.Validate(generation, *registry);
			if (!result)
				return std::unexpected(TranslateTagBuildError(
					generation,
					section.GetRegistryKey(),
					index,
					result.error()));
		}

		return {};
	}

	std::expected<void, StaticRegistryTagCollectionBuildError>
	StaticRegistryTagCollectionBuilder::Validate(
		RegistryGeneration generation,
		const StaticRegistryCollection& registries) const {
		if (generation == NoRegistryGeneration)
			return std::unexpected(StaticRegistryTagCollectionBuildError{
				.Code = StaticRegistryTagCollectionBuildErrorCode::InvalidGeneration,
				.Generation = generation,
			});

		for (std::size_t index{ 0 }; index < m_Sections.size(); ++index) {
			const auto& section = m_Sections[index];
			const auto* registry = registries.Find(section.GetRegistryKey());
			if (registry == nullptr)
				return std::unexpected(StaticRegistryTagCollectionBuildError{
					.Code = StaticRegistryTagCollectionBuildErrorCode::MissingRegistry,
					.Generation = generation,
					.RegistryKey = section.GetRegistryKey(),
					.SectionIndex = index,
				});

			const auto result = section.Validate(generation, *registry);
			if (!result)
				return std::unexpected(TranslateTagBuildError(
					generation,
					section.GetRegistryKey(),
					index,
					result.error()));
		}

		return {};
	}

	std::expected<StaticRegistryTagCollection, StaticRegistryTagCollectionBuildError>
	StaticRegistryTagCollectionBuilder::Build(
		RegistryGeneration generation,
		const StaticRegistryCollection& registries)&& {
		const auto validation = Validate(generation, registries);
		if (!validation) return std::unexpected(validation.error());

		std::vector<StaticRegistryTagSection> sections;
		sections.reserve(m_Sections.size());

		for (std::size_t index{ 0 }; index < m_Sections.size(); ++index) {
			auto& section = m_Sections[index];
			const auto* registry = registries.Find(section.GetRegistryKey());

			auto built = std::move(section).Build(generation, *registry);
			if (!built)
				return std::unexpected(TranslateTagBuildError(
					generation,
					section.GetRegistryKey(),
					index,
					built.error()));

			sections.push_back(std::move(*built));
		}

		return StaticRegistryTagCollection{
			generation,
			std::move(sections),
			std::move(m_KeyToIndex),
			m_TotalTags,
			m_TotalMembers,
		};
	}
}
