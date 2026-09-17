#include <Aurore/Util/RegistrySnapshot.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Util::RegistryKind;
	using Aurore::Util::ResourceLocation;

	struct TypedRegistryIdentity final {
		RegistryKind Kind;
		std::string_view Namespace;
		std::string_view Path;
	};

	constexpr std::array TypedRegistryIdentities{
		TypedRegistryIdentity{
			.Kind = RegistryKind::Block,
			.Namespace = "minecraft",
			.Path = "block",
		},
		TypedRegistryIdentity{
			.Kind = RegistryKind::Item,
			.Namespace = "minecraft",
			.Path = "item",
		},
		TypedRegistryIdentity{
			.Kind = RegistryKind::Biome,
			.Namespace = "minecraft",
			.Path = "worldgen/biome",
		},
		TypedRegistryIdentity{
			.Kind = RegistryKind::DimensionType,
			.Namespace = "minecraft",
			.Path = "dimension_type",
		},
	};

	[[nodiscard]] std::optional<RegistryKind> FindTypedRegistryKind(const ResourceLocation& key) noexcept {
		for (const auto& identity : TypedRegistryIdentities)
			if (key.GetNamespace() == identity.Namespace && key.GetPath() == identity.Path)
				return identity.Kind;
		return std::nullopt;
	}
}

namespace Aurore::Util {
	RegistrySnapshot::RegistrySnapshot(RegistryGeneration generation, BlockRegistry blocks, ItemRegistry items, BiomeRegistry biomes,
		DimensionTypeRegistry dimension_types, BlockTagSet block_tags, ItemTagSet item_tags, BiomeTagSet biome_tags, DimensionTypeTagSet dimension_type_tags,
		NetworkRegistryCollection network_registries, NetworkRegistryTagCollection network_registry_tags) noexcept
		: m_Generation(generation), m_Blocks(std::move(blocks)), m_Items(std::move(items)), m_Biomes(std::move(biomes)),
		m_DimensionTypes(std::move(dimension_types)), m_BlockTags(std::move(block_tags)), m_ItemTags(std::move(item_tags)), m_BiomeTags(std::move(biome_tags)),
		m_DimensionTypeTags(std::move(dimension_type_tags)), m_NetworkRegistries(std::move(network_registries)), m_NetworkRegistryTags(std::move(network_registry_tags)) {}

	RegistrySnapshotBuilder::RegistrySnapshotBuilder(RegistrySnapshotLimits limits) noexcept
		: m_Blocks(limits.Blocks), m_Items(limits.Items), m_Biomes(limits.Biomes), m_DimensionTypes(limits.DimensionTypes),
		m_BlockTags(limits.BlockTags), m_ItemTags(limits.ItemTags), m_BiomeTags(limits.BiomeTags), m_DimensionTypeTags(limits.DimensionTypeTags),
		m_NetworkRegistries(limits.NetworkRegistries), m_NetworkRegistryTags(limits.NetworkRegistryTags) {}

	std::expected<void, RegistrySnapshotError> RegistrySnapshotBuilder::ValidateGeneration(RegistryGeneration generation) noexcept {
		if (generation != NoRegistryGeneration) return {};
		return std::unexpected(RegistrySnapshotError{
			.Code = RegistrySnapshotErrorCode::InvalidGeneration,
			.Registry = std::nullopt,
			.Key = std::nullopt,
			.DeclarationIndex = 0,
			.ReferencedRegistry = std::nullopt,
			.ReferencedKey = std::nullopt,
			});
	}

	RegistrySnapshotError RegistrySnapshotBuilder::TranslateBuildError(RegistryKind registry, const RegistryBuildError& error) {
		RegistrySnapshotErrorCode code{ RegistrySnapshotErrorCode::ValidationRejected };
		switch (error.Code) {
		case RegistryBuildErrorCode::MissingReference:
			code = RegistrySnapshotErrorCode::MissingReference;
			break;
		case RegistryBuildErrorCode::InvalidDependency:
			code = RegistrySnapshotErrorCode::InvalidDependency;
			break;
		case RegistryBuildErrorCode::ValidationRejected:
			code = RegistrySnapshotErrorCode::ValidationRejected;
			break;
		}

		return RegistrySnapshotError{
			.Code = code,
			.Registry = registry,
			.Key = error.Key,
			.DeclarationIndex = error.DeclarationIndex,
			.ReferencedRegistry = std::nullopt,
			.ReferencedKey = error.ReferencedKey,
		};
	}

	RegistrySnapshotError RegistrySnapshotBuilder::TranslateTagBuildError(RegistryKind registry, const RegistryTagBuildError& error) {
		if (error.Code == RegistryTagBuildErrorCode::InvalidGeneration)
			return RegistrySnapshotError{
				.Code = RegistrySnapshotErrorCode::InvalidGeneration,
				.Registry = std::nullopt,
				.Key = std::nullopt,
				.DeclarationIndex = 0,
				.ReferencedRegistry = std::nullopt,
				.ReferencedKey = std::nullopt,
		};

		return RegistrySnapshotError{
			.Code = RegistrySnapshotErrorCode::MissingReference,
			.Registry = registry,
			.Key = error.TagKey,
			.DeclarationIndex = error.TagIndex,
			.ReferencedRegistry = registry,
			.ReferencedKey = error.MemberKey,
		};
	}

	std::expected<void, RegistrySnapshotError> RegistrySnapshotBuilder::ValidateTags(RegistryGeneration generation) const {
		const auto block_tags = m_BlockTags.Validate(generation, m_Blocks.GetDeclarationView());
		if (!block_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Block, block_tags.error()));

		const auto item_tags = m_ItemTags.Validate(generation, m_Items.GetDeclarationView());
		if (!item_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Item, item_tags.error()));

		const auto biome_tags = m_BiomeTags.Validate(generation, m_Biomes.GetDeclarationView());
		if (!biome_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Biome, biome_tags.error()));

		const auto dimension_type_tags = m_DimensionTypeTags.Validate(generation, m_DimensionTypes.GetDeclarationView());
		if (!dimension_type_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::DimensionType, dimension_type_tags.error()));

		const auto network_registry_tags = m_NetworkRegistryTags.Validate(generation, m_NetworkRegistries);
		if (!network_registry_tags)
			return std::unexpected(TranslateNetworkTagBuildError(network_registry_tags.error()));

		return {};
	}

	std::expected<std::shared_ptr<const RegistrySnapshot>, RegistrySnapshotError> RegistrySnapshotBuilder::Build(RegistryGeneration generation)&& {
		const auto generation_result = ValidateGeneration(generation);
		if (!generation_result) return std::unexpected(generation_result.error());

		const auto ownership_validation = ValidateNetworkRegistryOwnership();
		if (!ownership_validation) return std::unexpected(ownership_validation.error());

		const auto tag_validation = ValidateTags(generation);
		if (!tag_validation) return std::unexpected(tag_validation.error());

		return std::move(*this).Finalize(generation);
	}

	std::expected<void, RegistrySnapshotError> RegistrySnapshotBuilder::ValidateNetworkRegistryOwnership() const {
		const auto registries = m_NetworkRegistries.GetRegistries();
		for (std::size_t index{ 0 }; index < registries.size(); index++) {
			const auto typed_kind = FindTypedRegistryKind(registries[index].GetKey());
			if (!typed_kind) continue;
			return std::unexpected(RegistrySnapshotError{
				.Code = RegistrySnapshotErrorCode::RegistryRepresentationCollision,
				.Registry = *typed_kind,
				.Key = registries[index].GetKey(),
				.DeclarationIndex = index,
				.ReferencedRegistry = std::nullopt,
				.ReferencedKey = registries[index].GetKey(),
			});
		}
		return {};
	}

	RegistrySnapshotError RegistrySnapshotBuilder::TranslateNetworkTagBuildError(const NetworkRegistryTagCollectionBuildError& error) {
		if (error.Code == NetworkRegistryTagCollectionBuildErrorCode::InvalidGeneration)
			return RegistrySnapshotError{
				.Code = RegistrySnapshotErrorCode::InvalidGeneration,
				.Registry = std::nullopt,
				.Key = std::nullopt,
				.DeclarationIndex = 0,
				.ReferencedRegistry = std::nullopt,
				.ReferencedKey = std::nullopt,
			};


		if (error.Code == NetworkRegistryTagCollectionBuildErrorCode::MissingRegistry)
			return RegistrySnapshotError{
				.Code = RegistrySnapshotErrorCode::MissingReference,
				.Registry = std::nullopt,
				.Key = error.RegistryKey,
				.DeclarationIndex = error.SectionIndex,
				.ReferencedRegistry = std::nullopt,
				.ReferencedKey = error.RegistryKey,
			};


		return RegistrySnapshotError{
			.Code = RegistrySnapshotErrorCode::MissingReference,
			.Registry = std::nullopt,
			.Key = error.TagKey,
			.DeclarationIndex = error.TagIndex,
			.ReferencedRegistry = std::nullopt,
			.ReferencedKey = error.MemberKey,
		};
	}

	std::expected<std::shared_ptr<const RegistrySnapshot>, RegistrySnapshotError> RegistrySnapshotBuilder::Finalize(RegistryGeneration generation)&& {
		auto blocks = std::move(m_Blocks).Build();
		if (!blocks)
			return std::unexpected(TranslateBuildError(RegistryKind::Block, blocks.error()));

		auto items = std::move(m_Items).Build();
		if (!items)
			return std::unexpected(TranslateBuildError(RegistryKind::Item, items.error()));

		auto biomes = std::move(m_Biomes).Build();
		if (!biomes)
			return std::unexpected(TranslateBuildError(RegistryKind::Biome, biomes.error()));

		auto dimension_types = std::move(m_DimensionTypes).Build();
		if (!dimension_types)
			return std::unexpected(TranslateBuildError(RegistryKind::DimensionType, dimension_types.error()));

		auto block_tags = std::move(m_BlockTags).Build(generation, *blocks);
		if (!block_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Block, block_tags.error()));

		auto item_tags = std::move(m_ItemTags).Build(generation, *items);
		if (!item_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Item, item_tags.error()));

		auto biome_tags = std::move(m_BiomeTags).Build(generation, *biomes);
		if (!biome_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::Biome, biome_tags.error()));

		auto dimension_type_tags = std::move(m_DimensionTypeTags).Build(generation, *dimension_types);
		if (!dimension_type_tags)
			return std::unexpected(TranslateTagBuildError(RegistryKind::DimensionType, dimension_type_tags.error()));

		auto network_registries = std::move(m_NetworkRegistries).Build(generation);
		if (!network_registries)
			return std::unexpected(RegistrySnapshotError{
				.Code = RegistrySnapshotErrorCode::InvalidGeneration,
				.Registry = std::nullopt,
				.Key = std::nullopt,
				.DeclarationIndex = 0,
				.ReferencedRegistry = std::nullopt,
				.ReferencedKey = std::nullopt,
			});

		auto network_registry_tags = std::move(m_NetworkRegistryTags).Build(generation, *network_registries);
		if (!network_registry_tags) return std::unexpected(TranslateNetworkTagBuildError(network_registry_tags.error()));

		return std::shared_ptr<const RegistrySnapshot>{
			new RegistrySnapshot{
				generation,
				std::move(*blocks),
				std::move(*items),
				std::move(*biomes),
				std::move(*dimension_types),
				std::move(*block_tags),
				std::move(*item_tags),
				std::move(*biome_tags),
				std::move(*dimension_type_tags),
				std::move(*network_registries),
				std::move(*network_registry_tags),
			}
		};
	}
}
