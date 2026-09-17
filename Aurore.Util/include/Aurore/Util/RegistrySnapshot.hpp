#pragma once

#include "Nbt.hpp"
#include "Registry.hpp"
#include "RegistryTag.hpp"
#include "NetworkRegistry.hpp"

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace Aurore::Util {
	struct BlockDefinition final {
		NbtCompound Data;

		bool operator==(const BlockDefinition&) const noexcept = default;
	};

	struct ItemDefinition final {
		NbtCompound Data;

		bool operator==(const ItemDefinition&) const noexcept = default;
	};

	struct BiomeDefinition final {
		NbtCompound Data;

		bool operator==(const BiomeDefinition&) const noexcept = default;
	};

	struct DimensionTypeDefinition final {
		NbtCompound Data;

		bool operator==(const DimensionTypeDefinition&) const noexcept = default;
	};

	using BlockRegistry = Registry<BlockDefinition>;
	using ItemRegistry = Registry<ItemDefinition>;
	using BiomeRegistry = Registry<BiomeDefinition>;
	using DimensionTypeRegistry = Registry<DimensionTypeDefinition>;

	using BlockRegistryBuilder = RegistryBuilder<BlockDefinition>;
	using ItemRegistryBuilder = RegistryBuilder<ItemDefinition>;
	using BiomeRegistryBuilder = RegistryBuilder<BiomeDefinition>;
	using DimensionTypeRegistryBuilder = RegistryBuilder<DimensionTypeDefinition>;

	using BlockRegistryDeclarationView = RegistryDeclarationView<BlockDefinition>;
	using ItemRegistryDeclarationView = RegistryDeclarationView<ItemDefinition>;
	using BiomeRegistryDeclarationView = RegistryDeclarationView<BiomeDefinition>;
	using DimensionTypeRegistryDeclarationView = RegistryDeclarationView<DimensionTypeDefinition>;

	using BlockTagSet = RegistryTagSet<BlockDefinition>;
	using ItemTagSet = RegistryTagSet<ItemDefinition>;
	using BiomeTagSet = RegistryTagSet<BiomeDefinition>;
	using DimensionTypeTagSet = RegistryTagSet<DimensionTypeDefinition>;

	using BlockTagBuilder = RegistryTagBuilder<BlockDefinition>;
	using ItemTagBuilder = RegistryTagBuilder<ItemDefinition>;
	using BiomeTagBuilder = RegistryTagBuilder<BiomeDefinition>;
	using DimensionTypeTagBuilder = RegistryTagBuilder<DimensionTypeDefinition>;

	using BlockTagDeclarationView = RegistryTagDeclarationView<BlockDefinition>;
	using ItemTagDeclarationView = RegistryTagDeclarationView<ItemDefinition>;
	using BiomeTagDeclarationView = RegistryTagDeclarationView<BiomeDefinition>;
	using DimensionTypeTagDeclarationView = RegistryTagDeclarationView<DimensionTypeDefinition>;

	struct RegistrySnapshotLimits final {
		RegistryLimits Blocks{};
		RegistryLimits Items{};
		RegistryLimits Biomes{};
		RegistryLimits DimensionTypes{};

		RegistryTagLimits BlockTags{};
		RegistryTagLimits ItemTags{};
		RegistryTagLimits BiomeTags{};
		RegistryTagLimits DimensionTypeTags{};

		NetworkRegistryCollectionLimits NetworkRegistries{};
		NetworkRegistryTagCollectionLimits NetworkRegistryTags{};
	};

	enum class RegistryKind : std::uint8_t {
		Block,
		Item,
		Biome,
		DimensionType,
	};

	enum class RegistrySnapshotErrorCode : std::uint8_t {
		InvalidGeneration,
		MissingReference,
		InvalidDependency,
		ValidationRejected,
		RegistryRepresentationCollision,
	};

	struct RegistrySnapshotError final {
		RegistrySnapshotErrorCode Code;
		std::optional<RegistryKind> Registry;
		std::optional<ResourceLocation> Key;
		std::size_t DeclarationIndex{ 0 };
		std::optional<RegistryKind> ReferencedRegistry;
		std::optional<ResourceLocation> ReferencedKey;

		auto operator<=>(const RegistrySnapshotError&) const noexcept = default;
	};

	class RegistrySnapshotDeclarationView final {
	public:
		[[nodiscard]] const BlockRegistryDeclarationView& GetBlocks() const noexcept { return *m_Blocks; }
		[[nodiscard]] const ItemRegistryDeclarationView& GetItems() const noexcept { return *m_Items; }
		[[nodiscard]] const BiomeRegistryDeclarationView& GetBiomes() const noexcept { return *m_Biomes; }
		[[nodiscard]] const DimensionTypeRegistryDeclarationView& GetDimensionTypes() const noexcept { return *m_DimensionTypes; }
		[[nodiscard]] const BlockTagDeclarationView& GetBlockTags() const noexcept { return *m_BlockTags; }
		[[nodiscard]] const ItemTagDeclarationView& GetItemTags() const noexcept { return *m_ItemTags; }
		[[nodiscard]] const BiomeTagDeclarationView& GetBiomeTags() const noexcept { return *m_BiomeTags; }
		[[nodiscard]] const DimensionTypeTagDeclarationView& GetDimensionTypeTags() const noexcept { return *m_DimensionTypeTags; }
		[[nodiscard]] const NetworkRegistryCollectionBuilder& GetNetworkRegistries() const noexcept { return *m_NetworkRegistries; }
		[[nodiscard]] const NetworkRegistryTagCollectionBuilder& GetNetworkRegistryTags() const noexcept { return *m_NetworkRegistryTags; }

	private:
		RegistrySnapshotDeclarationView(
			const BlockRegistryDeclarationView& blocks,
			const ItemRegistryDeclarationView& items,
			const BiomeRegistryDeclarationView& biomes,
			const DimensionTypeRegistryDeclarationView& dimension_types,
			const BlockTagDeclarationView& block_tags,
			const ItemTagDeclarationView& item_tags,
			const BiomeTagDeclarationView& biome_tags,
			const DimensionTypeTagDeclarationView& dimension_type_tags,
			const NetworkRegistryCollectionBuilder& network_registries,
			const NetworkRegistryTagCollectionBuilder& network_registry_tags) noexcept
			: m_Blocks(&blocks),
			m_Items(&items),
			m_Biomes(&biomes),
			m_DimensionTypes(&dimension_types),
			m_BlockTags(&block_tags),
			m_ItemTags(&item_tags),
			m_BiomeTags(&biome_tags),
			m_DimensionTypeTags(&dimension_type_tags),
			m_NetworkRegistries(&network_registries),
			m_NetworkRegistryTags(&network_registry_tags) {}

		const BlockRegistryDeclarationView* m_Blocks;
		const ItemRegistryDeclarationView* m_Items;
		const BiomeRegistryDeclarationView* m_Biomes;
		const DimensionTypeRegistryDeclarationView* m_DimensionTypes;
		const BlockTagDeclarationView* m_BlockTags;
		const ItemTagDeclarationView* m_ItemTags;
		const BiomeTagDeclarationView* m_BiomeTags;
		const DimensionTypeTagDeclarationView* m_DimensionTypeTags;
		const NetworkRegistryCollectionBuilder* m_NetworkRegistries;
		const NetworkRegistryTagCollectionBuilder* m_NetworkRegistryTags;

		friend class RegistrySnapshotBuilder;
	};

	class RegistrySnapshot final {
	public:
		RegistrySnapshot(const RegistrySnapshot&) = delete;
		RegistrySnapshot& operator=(const RegistrySnapshot&) = delete;
		RegistrySnapshot(RegistrySnapshot&&) = delete;
		RegistrySnapshot& operator=(RegistrySnapshot&&) = delete;

		[[nodiscard]] RegistryGeneration GetGeneration() const noexcept { return m_Generation; }
		[[nodiscard]] const BlockRegistry& GetBlocks() const noexcept { return m_Blocks; }
		[[nodiscard]] const ItemRegistry& GetItems() const noexcept { return m_Items; }
		[[nodiscard]] const BiomeRegistry& GetBiomes() const noexcept { return m_Biomes; }
		[[nodiscard]] const DimensionTypeRegistry& GetDimensionTypes() const noexcept { return m_DimensionTypes; }
		[[nodiscard]] const BlockTagSet& GetBlockTags() const noexcept { return m_BlockTags; }
		[[nodiscard]] const ItemTagSet& GetItemTags() const noexcept { return m_ItemTags; }
		[[nodiscard]] const BiomeTagSet& GetBiomeTags() const noexcept { return m_BiomeTags; }
		[[nodiscard]] const DimensionTypeTagSet& GetDimensionTypeTags() const noexcept { return m_DimensionTypeTags; }
		[[nodiscard]] const NetworkRegistryCollection& GetNetworkRegistries() const noexcept { return m_NetworkRegistries; }
		[[nodiscard]] const NetworkRegistryTagCollection& GetNetworkRegistryTags() const noexcept { return m_NetworkRegistryTags; }

	private:
		RegistrySnapshot(
			RegistryGeneration generation,
			BlockRegistry blocks,
			ItemRegistry items,
			BiomeRegistry biomes,
			DimensionTypeRegistry dimension_types,
			BlockTagSet block_tags,
			ItemTagSet item_tags,
			BiomeTagSet biome_tags,
			DimensionTypeTagSet dimension_type_tags,
			NetworkRegistryCollection network_registries,
			NetworkRegistryTagCollection network_registry_tags) noexcept;

		RegistryGeneration m_Generation;
		BlockRegistry m_Blocks;
		ItemRegistry m_Items;
		BiomeRegistry m_Biomes;
		DimensionTypeRegistry m_DimensionTypes;
		BlockTagSet m_BlockTags;
		ItemTagSet m_ItemTags;
		BiomeTagSet m_BiomeTags;
		DimensionTypeTagSet m_DimensionTypeTags;
		NetworkRegistryCollection m_NetworkRegistries;
		NetworkRegistryTagCollection m_NetworkRegistryTags;

		friend class RegistrySnapshotBuilder;
	};

	class RegistrySnapshotBuilder final {
	public:
		RegistrySnapshotBuilder() = default;
		explicit RegistrySnapshotBuilder(RegistrySnapshotLimits limits) noexcept;

		RegistrySnapshotBuilder(const RegistrySnapshotBuilder&) = delete;
		RegistrySnapshotBuilder& operator=(const RegistrySnapshotBuilder&) = delete;
		RegistrySnapshotBuilder(RegistrySnapshotBuilder&&) noexcept = default;
		RegistrySnapshotBuilder& operator=(RegistrySnapshotBuilder&&) noexcept = default;

		[[nodiscard]] BlockRegistryBuilder& Blocks() noexcept { return m_Blocks; }
		[[nodiscard]] const BlockRegistryBuilder& Blocks() const noexcept { return m_Blocks; }
		[[nodiscard]] ItemRegistryBuilder& Items() noexcept { return m_Items; }
		[[nodiscard]] const ItemRegistryBuilder& Items() const noexcept { return m_Items; }
		[[nodiscard]] BiomeRegistryBuilder& Biomes() noexcept { return m_Biomes; }
		[[nodiscard]] const BiomeRegistryBuilder& Biomes() const noexcept { return m_Biomes; }
		[[nodiscard]] DimensionTypeRegistryBuilder& DimensionTypes() noexcept { return m_DimensionTypes; }
		[[nodiscard]] const DimensionTypeRegistryBuilder& DimensionTypes() const noexcept { return m_DimensionTypes; }
		[[nodiscard]] BlockTagBuilder& BlockTags() noexcept { return m_BlockTags; }
		[[nodiscard]] const BlockTagBuilder& BlockTags() const noexcept { return m_BlockTags; }
		[[nodiscard]] ItemTagBuilder& ItemTags() noexcept { return m_ItemTags; }
		[[nodiscard]] const ItemTagBuilder& ItemTags() const noexcept { return m_ItemTags; }
		[[nodiscard]] BiomeTagBuilder& BiomeTags() noexcept { return m_BiomeTags; }
		[[nodiscard]] const BiomeTagBuilder& BiomeTags() const noexcept { return m_BiomeTags; }
		[[nodiscard]] DimensionTypeTagBuilder& DimensionTypeTags() noexcept { return m_DimensionTypeTags; }
		[[nodiscard]] const DimensionTypeTagBuilder& DimensionTypeTags() const noexcept { return m_DimensionTypeTags; }
		[[nodiscard]] NetworkRegistryCollectionBuilder& NetworkRegistries() noexcept { return m_NetworkRegistries; }
		[[nodiscard]] const NetworkRegistryCollectionBuilder& NetworkRegistries() const noexcept { return m_NetworkRegistries; }
		[[nodiscard]] NetworkRegistryTagCollectionBuilder& NetworkRegistryTags() noexcept { return m_NetworkRegistryTags; }
		[[nodiscard]] const NetworkRegistryTagCollectionBuilder& NetworkRegistryTags() const noexcept { return m_NetworkRegistryTags; }

		[[nodiscard]] std::expected<std::shared_ptr<const RegistrySnapshot>, RegistrySnapshotError> Build(RegistryGeneration generation)&&;

		template<typename Validator>
		requires std::same_as<
			std::invoke_result_t<Validator&, const RegistrySnapshotDeclarationView&>,
			std::expected<void, RegistrySnapshotError>>
		[[nodiscard]] std::expected<void, RegistrySnapshotError> Validate(Validator&& validator) const {
			const auto blocks = m_Blocks.GetDeclarationView();
			const auto items = m_Items.GetDeclarationView();
			const auto biomes = m_Biomes.GetDeclarationView();
			const auto dimension_types = m_DimensionTypes.GetDeclarationView();
			const auto block_tags = m_BlockTags.GetDeclarationView();
			const auto item_tags = m_ItemTags.GetDeclarationView();
			const auto biome_tags = m_BiomeTags.GetDeclarationView();
			const auto dimension_type_tags = m_DimensionTypeTags.GetDeclarationView();

			const RegistrySnapshotDeclarationView declarations{
				blocks, items, biomes, dimension_types,
				block_tags, item_tags, biome_tags, dimension_type_tags,
				m_NetworkRegistries, m_NetworkRegistryTags
			};

			return std::invoke(std::forward<Validator>(validator), declarations);
		}

		template<typename Validator>
		requires std::same_as<
			std::invoke_result_t<Validator&, const RegistrySnapshotDeclarationView&>,
			std::expected<void, RegistrySnapshotError>>
		[[nodiscard]] std::expected<std::shared_ptr<const RegistrySnapshot>, RegistrySnapshotError> Build(RegistryGeneration generation, Validator&& validator)&& {
			const auto generation_result = ValidateGeneration(generation);
			if (!generation_result) return std::unexpected(generation_result.error());

			const auto ownership_validation = ValidateNetworkRegistryOwnership();
			if (!ownership_validation) return std::unexpected(ownership_validation.error());

			const auto validation = Validate(std::forward<Validator>(validator));
			if (!validation) return std::unexpected(validation.error());

			const auto tag_validation = ValidateTags(generation);
			if (!tag_validation) return std::unexpected(tag_validation.error());

			return std::move(*this).Finalize(generation);
		}

	private:
		[[nodiscard]] static std::expected<void, RegistrySnapshotError> ValidateGeneration(RegistryGeneration generation) noexcept;
		[[nodiscard]] static RegistrySnapshotError TranslateBuildError(RegistryKind registry, const RegistryBuildError& error);
		[[nodiscard]] static RegistrySnapshotError TranslateTagBuildError(RegistryKind registry, const RegistryTagBuildError& error);
		[[nodiscard]] std::expected<void, RegistrySnapshotError> ValidateTags(RegistryGeneration generation) const;
		[[nodiscard]] std::expected<std::shared_ptr<const RegistrySnapshot>, RegistrySnapshotError> Finalize(RegistryGeneration generation)&&;
		[[nodiscard]] std::expected<void, RegistrySnapshotError> ValidateNetworkRegistryOwnership() const;
		[[nodiscard]] static RegistrySnapshotError TranslateNetworkTagBuildError(const NetworkRegistryTagCollectionBuildError& error);

		BlockRegistryBuilder m_Blocks;
		ItemRegistryBuilder m_Items;
		BiomeRegistryBuilder m_Biomes;
		DimensionTypeRegistryBuilder m_DimensionTypes;
		BlockTagBuilder m_BlockTags;
		ItemTagBuilder m_ItemTags;
		BiomeTagBuilder m_BiomeTags;
		DimensionTypeTagBuilder m_DimensionTypeTags;
		NetworkRegistryCollectionBuilder m_NetworkRegistries;
		NetworkRegistryTagCollectionBuilder m_NetworkRegistryTags;
	};
}
