#pragma once

#include "Registry.hpp"

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
	struct RegistryTagLimits final {
		std::size_t MaximumTags{ 65'536 };
		std::size_t MaximumMembersPerTag{ 1'048'576 };
		std::size_t MaximumTotalMembers{ 4'194'304 };
	};

	enum class RegistryTagDeclarationErrorCode : std::uint8_t {
		DuplicateTagKey,
		TagLimitExceeded,
		MemberLimitExceeded,
		TotalMemberLimitExceeded,
		DuplicateMember,
	};

	struct RegistryTagDeclarationError final {
		RegistryTagDeclarationErrorCode Code;
		ResourceLocation TagKey;
		std::size_t DeclarationIndex{ 0 };
		std::optional<std::size_t> ExistingIndex;
		std::optional<ResourceLocation> MemberKey;
		std::optional<std::size_t> MemberIndex;
		std::optional<std::size_t> ExistingMemberIndex;

		auto operator<=>(const RegistryTagDeclarationError&) const noexcept = default;
	};
	enum class RegistryTagBuildErrorCode : std::uint8_t {
		InvalidGeneration,
		MissingMember,
	};

	struct RegistryTagBuildError final {
		RegistryTagBuildErrorCode Code;
		std::optional<ResourceLocation> TagKey;
		std::size_t TagIndex{ 0 };
		std::optional<ResourceLocation> MemberKey;
		std::optional<std::size_t> MemberIndex;

		auto operator<=>(const RegistryTagBuildError&) const noexcept = default;
	};

	struct RegistryTagDeclaration final {
		ResourceLocation Key;
		std::vector<ResourceLocation> Members;
	};

	template<typename T>
	struct RegistryTag final {
		ResourceLocation Key;
		std::vector<RegistryRuntimeId> Members;
	};

	template<typename T>
	class RegistryTagBuilder;

	template<typename T>
	class RegistryTagDeclarationView final {
	public:
		[[nodiscard]] bool Empty() const noexcept { return m_Declarations.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Declarations.size(); }
		[[nodiscard]] std::span<const RegistryTagDeclaration> GetDeclarations() const noexcept { return m_Declarations; }

		[[nodiscard]] const RegistryTagDeclaration* Find(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToIndex->find(key);
			if (iterator == m_KeyToIndex->end()) return nullptr;
			return &m_Declarations[iterator->second];
		}

		[[nodiscard]] std::optional<std::size_t> FindIndex(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToIndex->find(key);
			if (iterator == m_KeyToIndex->end()) return std::nullopt;
			return iterator->second;
		}

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToIndex->contains(key);
		}

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		RegistryTagDeclarationView(
			std::span<const RegistryTagDeclaration> declarations,
			const KeyIndex& key_to_index) noexcept
			: m_Declarations(declarations), m_KeyToIndex(&key_to_index) {}

		std::span<const RegistryTagDeclaration> m_Declarations;
		const KeyIndex* m_KeyToIndex;

		friend class RegistryTagBuilder<T>;
	};

	template<typename T>
	class RegistryTagSet final {
	public:
		using Tag = RegistryTag<T>;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] bool Empty() const noexcept { return m_Tags.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Tags.size(); }
		[[nodiscard]] std::span<const Tag> GetTags() const noexcept { return m_Tags; }

		[[nodiscard]] const Tag* Find(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToIndex.find(key);
			if (iterator == m_KeyToIndex.end()) return nullptr;
			return &m_Tags[iterator->second];
		}

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToIndex.contains(key);
		}

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		RegistryTagSet(
			RegistryGeneration generation,
			std::vector<Tag> tags,
			KeyIndex key_to_index) noexcept
			: m_Generation(generation),
			m_Tags(std::move(tags)),
			m_KeyToIndex(std::move(key_to_index)) {}

		RegistryGeneration m_Generation{ NoRegistryGeneration };
		std::vector<Tag> m_Tags;
		KeyIndex m_KeyToIndex;

		friend class RegistryTagBuilder<T>;
	};

	template<typename T>
	class RegistryTagBuilder final {
	public:
		using DeclarationView = RegistryTagDeclarationView<T>;
		using FinalTagSet = RegistryTagSet<T>;

		RegistryTagBuilder() = default;
		explicit RegistryTagBuilder(RegistryTagLimits limits) noexcept
			: m_Limits(limits) {}

		[[nodiscard]] bool Empty() const noexcept { return m_Declarations.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Declarations.size(); }
		[[nodiscard]] std::size_t TotalMemberCount() const noexcept { return m_TotalMembers; }
		[[nodiscard]] RegistryTagLimits GetLimits() const noexcept { return m_Limits; }

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToIndex.contains(key);
		}

		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept {
			return DeclarationView{ m_Declarations, m_KeyToIndex };
		}

		[[nodiscard]] std::expected<void, RegistryTagDeclarationError> Declare(
			ResourceLocation key,
			std::vector<ResourceLocation> members) {

			const auto declaration_index = m_Declarations.size();
			if (declaration_index >= m_Limits.MaximumTags)
				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::TagLimitExceeded,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					.ExistingMemberIndex = std::nullopt,
					});

			const auto existing_tag = m_KeyToIndex.find(key);
			if (existing_tag != m_KeyToIndex.end())
				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::DuplicateTagKey,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = existing_tag->second,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					.ExistingMemberIndex = std::nullopt,
					});

			if (members.size() > m_Limits.MaximumMembersPerTag)
				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::MemberLimitExceeded,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					.ExistingMemberIndex = std::nullopt,
					});

			if (m_TotalMembers > m_Limits.MaximumTotalMembers
				|| members.size() > m_Limits.MaximumTotalMembers - m_TotalMembers)
				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::TotalMemberLimitExceeded,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					.ExistingMemberIndex = std::nullopt,
					});

			std::unordered_map<ResourceLocation, std::size_t> member_indices;
			member_indices.reserve(members.size());
			for (std::size_t member_index{ 0 }; member_index < members.size(); ++member_index) {
				const auto [iterator, inserted] = member_indices.emplace(members[member_index], member_index);
				if (inserted) continue;

				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::DuplicateMember,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
					.MemberKey = members[member_index],
					.MemberIndex = member_index,
					.ExistingMemberIndex = iterator->second,
					});
			}

			const auto [iterator, inserted] = m_KeyToIndex.emplace(key, declaration_index);
			if (!inserted)
				return std::unexpected(RegistryTagDeclarationError{
					.Code = RegistryTagDeclarationErrorCode::DuplicateTagKey,
					.TagKey = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = iterator->second,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					.ExistingMemberIndex = std::nullopt,
					});

			try {
				m_Declarations.push_back(RegistryTagDeclaration{
					.Key = std::move(key),
					.Members = std::move(members),
					});
			}
			catch (...) {
				m_KeyToIndex.erase(iterator);
				throw;
			}

			m_TotalMembers += m_Declarations.back().Members.size();
			return {};
		}

		template<typename TMetadata>
		[[nodiscard]] std::expected<void, RegistryTagBuildError> Validate(
			RegistryGeneration generation,
			const RegistryDeclarationView<T, TMetadata>& registry) const {

			return ValidateMembers(generation, [&registry](const ResourceLocation& key) noexcept {
				return registry.Contains(key);
				});
		}

		template<typename TMetadata>
		[[nodiscard]] std::expected<void, RegistryTagBuildError> Validate(
			RegistryGeneration generation,
			const Registry<T, TMetadata>& registry) const {

			return ValidateMembers(generation, [&registry](const ResourceLocation& key) noexcept {
				return registry.Contains(key);
				});
		}

		template<typename TMetadata>
		[[nodiscard]] std::expected<FinalTagSet, RegistryTagBuildError> Build(
			RegistryGeneration generation,
			const Registry<T, TMetadata>& registry)&& {

			const auto validation = Validate(generation, registry);
			if (!validation) return std::unexpected(validation.error());

			using Tag = typename FinalTagSet::Tag;
			std::vector<Tag> tags;
			tags.reserve(m_Declarations.size());

			for (auto& declaration : m_Declarations) {
				std::vector<RegistryRuntimeId> members;
				members.reserve(declaration.Members.size());
				for (const auto& member : declaration.Members)
					members.push_back(*registry.FindRuntimeId(member));

				tags.push_back(Tag{
					.Key = std::move(declaration.Key),
					.Members = std::move(members),
					});
			}

			return FinalTagSet{
				generation,
				std::move(tags),
				std::move(m_KeyToIndex),
			};
		}

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, std::size_t>;

		template<typename Contains>
		[[nodiscard]] std::expected<void, RegistryTagBuildError> ValidateMembers(
			RegistryGeneration generation,
			Contains&& contains) const {

			if (generation == NoRegistryGeneration)
				return std::unexpected(RegistryTagBuildError{
					.Code = RegistryTagBuildErrorCode::InvalidGeneration,
					.TagKey = std::nullopt,
					.TagIndex = 0,
					.MemberKey = std::nullopt,
					.MemberIndex = std::nullopt,
					});

			for (std::size_t tag_index{ 0 }; tag_index < m_Declarations.size(); ++tag_index) {
				const auto& declaration = m_Declarations[tag_index];
				for (std::size_t member_index{ 0 }; member_index < declaration.Members.size(); ++member_index) {
					const auto& member = declaration.Members[member_index];
					if (contains(member)) continue;

					return std::unexpected(RegistryTagBuildError{
						.Code = RegistryTagBuildErrorCode::MissingMember,
						.TagKey = declaration.Key,
						.TagIndex = tag_index,
						.MemberKey = member,
						.MemberIndex = member_index,
						});
				}
			}

			return {};
		}

		RegistryTagLimits m_Limits;
		std::vector<RegistryTagDeclaration> m_Declarations;
		KeyIndex m_KeyToIndex;
		std::size_t m_TotalMembers{ 0 };
	};
}
