#pragma once

#include "ResourceLocation.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Util {
	using RegistryRuntimeId = std::uint32_t;
	using RegistryGeneration = std::uint64_t;

	inline constexpr RegistryGeneration NoRegistryGeneration{ 0 };

	struct RegistryLimits final {
		std::size_t MaximumEntries{ 1'048'576 };
	};

	enum class RegistryDeclarationErrorCode : std::uint8_t {
		DuplicateKey,
		EntryLimitExceeded,
		RuntimeIdSpaceExhausted,
	};

	struct RegistryDeclarationError final {
		RegistryDeclarationErrorCode Code;
		ResourceLocation Key;
		std::size_t DeclarationIndex{ 0 };
		std::optional<std::size_t> ExistingIndex;

		auto operator<=>(const RegistryDeclarationError&) const noexcept = default;
	};

	enum class RegistryBuildErrorCode : std::uint8_t {
		MissingReference,
		InvalidDependency,
		ValidationRejected,
	};

	struct RegistryBuildError final {
		RegistryBuildErrorCode Code;
		ResourceLocation Key;
		std::size_t DeclarationIndex{ 0 };
		std::optional<ResourceLocation> ReferencedKey;

		auto operator<=>(const RegistryBuildError&) const noexcept = default;
	};

	template<typename T, typename TMetadata = std::monostate>
	struct RegistryDeclaration final {
		ResourceLocation Key;
		T Value;
		[[no_unique_address]] TMetadata Metadata;
	};

	template<typename T, typename TMetadata = std::monostate>
	struct RegistryEntry final {
		ResourceLocation Key;
		RegistryRuntimeId RuntimeId;
		T Value;
		[[no_unique_address]] TMetadata Metadata;
	};

	template<typename T, typename TMetadata>
	class RegistryBuilder;

	template<typename T, typename TMetadata = std::monostate>
	class RegistryDeclarationView final {
	public:
		using Declaration = RegistryDeclaration<T, TMetadata>;

		[[nodiscard]] bool Empty() const noexcept { return m_Declarations.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Declarations.size(); }
		[[nodiscard]] std::span<const Declaration> GetDeclarations() const noexcept { return m_Declarations; }

		[[nodiscard]] const Declaration* Find(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToIndex->find(key);
			if (iterator == m_KeyToIndex->end()) return nullptr;
			return &m_Declarations[static_cast<std::size_t>(iterator->second)];
		}

		[[nodiscard]] std::optional<std::size_t> FindIndex(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToIndex->find(key);
			if (iterator == m_KeyToIndex->end()) return std::nullopt;
			return static_cast<std::size_t>(iterator->second);
		}

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToIndex->contains(key);
		}

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, RegistryRuntimeId>;

		RegistryDeclarationView(std::span<const Declaration> declarations, const KeyIndex& key_to_index) noexcept
			: m_Declarations(declarations), m_KeyToIndex(&key_to_index) {}

		std::span<const Declaration> m_Declarations;
		const KeyIndex* m_KeyToIndex;

		friend class RegistryBuilder<T, TMetadata>;
	};

	template<typename T, typename TMetadata = std::monostate>
	class Registry final {
	public:
		using Entry = RegistryEntry<T, TMetadata>;

		[[nodiscard]] bool Empty() const noexcept { return m_Entries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }
		[[nodiscard]] std::span<const Entry> GetEntries() const noexcept { return m_Entries; }

		[[nodiscard]] const Entry* Find(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToRuntimeId.find(key);
			if (iterator == m_KeyToRuntimeId.end()) return nullptr;
			return Find(iterator->second);
		}

		[[nodiscard]] const Entry* Find(RegistryRuntimeId runtime_id) const noexcept {
			const auto index = static_cast<std::size_t>(runtime_id);
			if (index >= m_Entries.size()) return nullptr;
			return &m_Entries[index];
		}

		[[nodiscard]] std::optional<RegistryRuntimeId> FindRuntimeId(const ResourceLocation& key) const noexcept {
			const auto iterator = m_KeyToRuntimeId.find(key);
			if (iterator == m_KeyToRuntimeId.end()) return std::nullopt;
			return iterator->second;
		}

		[[nodiscard]] const ResourceLocation* FindKey(RegistryRuntimeId runtime_id) const noexcept {
			const auto entry = Find(runtime_id);
			return entry == nullptr ? nullptr : &entry->Key;
		}

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToRuntimeId.contains(key);
		}

	private:
		using KeyIndex = std::unordered_map<ResourceLocation, RegistryRuntimeId>;

		Registry(std::vector<Entry> entries, KeyIndex key_to_runtime_id) noexcept
			: m_Entries(std::move(entries)), m_KeyToRuntimeId(std::move(key_to_runtime_id)) {}

		std::vector<Entry> m_Entries;
		KeyIndex m_KeyToRuntimeId;

		friend class RegistryBuilder<T, TMetadata>;
	};

	template<typename T, typename TMetadata = std::monostate>
	class RegistryBuilder final {
	public:
		using Declaration = RegistryDeclaration<T, TMetadata>;
		using DeclarationView = RegistryDeclarationView<T, TMetadata>;
		using FinalRegistry = Registry<T, TMetadata>;

		RegistryBuilder() = default;
		explicit RegistryBuilder(RegistryLimits limits) noexcept
			: m_Limits(limits) {}

		[[nodiscard]] bool Empty() const noexcept { return m_Declarations.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Declarations.size(); }
		[[nodiscard]] RegistryLimits GetLimits() const noexcept { return m_Limits; }

		[[nodiscard]] bool Contains(const ResourceLocation& key) const noexcept {
			return m_KeyToIndex.contains(key);
		}

		[[nodiscard]] DeclarationView GetDeclarationView() const noexcept {
			return DeclarationView{ m_Declarations, m_KeyToIndex, };
		}

		[[nodiscard]] std::expected<void, RegistryDeclarationError> Declare(ResourceLocation key, T value)
			requires std::default_initializable<TMetadata> {
			return Declare(std::move(key), std::move(value), TMetadata{});
		}

		[[nodiscard]] std::expected<void, RegistryDeclarationError> Declare(ResourceLocation key, T value, TMetadata metadata) {
			const auto declaration_index = m_Declarations.size();
			if (declaration_index >= m_Limits.MaximumEntries)
				return std::unexpected(RegistryDeclarationError{
					.Code = RegistryDeclarationErrorCode::EntryLimitExceeded,
					.Key = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
				});

			if (declaration_index > static_cast<std::size_t>(std::numeric_limits<RegistryRuntimeId>::max()))
				return std::unexpected(RegistryDeclarationError{
					.Code = RegistryDeclarationErrorCode::RuntimeIdSpaceExhausted,
					.Key = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = std::nullopt,
				});

			const auto runtime_id = static_cast<RegistryRuntimeId>(declaration_index);
			const auto [iterator, inserted] = m_KeyToIndex.emplace(key, runtime_id);
			if (!inserted)
				return std::unexpected(RegistryDeclarationError{
					.Code = RegistryDeclarationErrorCode::DuplicateKey,
					.Key = std::move(key),
					.DeclarationIndex = declaration_index,
					.ExistingIndex = static_cast<std::size_t>(iterator->second),
				});

			try {
				m_Declarations.push_back(Declaration{
					.Key = std::move(key),
					.Value = std::move(value),
					.Metadata = std::move(metadata),
				});
			}
			catch (...) {
				m_KeyToIndex.erase(iterator);
				throw;
			}

			return {};
		}

		[[nodiscard]] std::expected<FinalRegistry, RegistryBuildError> Build() && {
			return std::move(*this).Finalize();
		}

		template<typename Validator>
		requires std::same_as<
			std::invoke_result_t<Validator&, const DeclarationView&>,
			std::expected<void, RegistryBuildError>>
		[[nodiscard]] std::expected<FinalRegistry, RegistryBuildError> Build(Validator&& validator) && {
			const auto validation = Validate(std::forward<Validator>(validator));
			if (!validation) return std::unexpected(validation.error());
			return std::move(*this).Finalize();
		}

		template<typename Validator>
		requires std::same_as<
			std::invoke_result_t<Validator&, const DeclarationView&>,
			std::expected<void, RegistryBuildError>>
		[[nodiscard]] std::expected<void, RegistryBuildError> Validate(Validator&& validator) const {
			const auto declarations = GetDeclarationView();
			return std::invoke(std::forward<Validator>(validator), declarations);
		}

	private:
		using Entry = RegistryEntry<T, TMetadata>;
		using KeyIndex = std::unordered_map<ResourceLocation, RegistryRuntimeId>;

		[[nodiscard]] FinalRegistry Finalize() && {
			std::vector<Entry> entries;
			entries.reserve(m_Declarations.size());

			for (std::size_t index{ 0 }; index < m_Declarations.size(); ++index) {
				auto& declaration = m_Declarations[index];
				entries.push_back(Entry{
					.Key = std::move(declaration.Key),
					.RuntimeId = static_cast<RegistryRuntimeId>(index),
					.Value = std::move(declaration.Value),
					.Metadata = std::move(declaration.Metadata),
				});
			}

			return FinalRegistry{ std::move(entries), std::move(m_KeyToIndex) };
		}

		RegistryLimits m_Limits;
		std::vector<Declaration> m_Declarations;
		KeyIndex m_KeyToIndex;
	};
}

