#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Util/Nbt.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
	using Aurore::Core::SyntheticRegistrySnapshotError;
	using Aurore::Core::SyntheticRegistrySnapshotErrorCode;
	using Aurore::Util::BiomeDefinition;
	using Aurore::Util::BlockDefinition;
	using Aurore::Util::DimensionTypeDefinition;
	using Aurore::Util::ItemDefinition;
	using Aurore::Util::NbtCompound;
	using Aurore::Util::NbtString;
	using Aurore::Util::NbtValue;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistryKind;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::RegistrySnapshotBuilder;
	using Aurore::Util::RegistrySnapshotDeclarationView;
	using Aurore::Util::RegistrySnapshotError;
	using Aurore::Util::RegistrySnapshotErrorCode;
	using Aurore::Util::ResourceLocation;

	class SyntheticRegistrySnapshotAssembler final {
	public:
		explicit SyntheticRegistrySnapshotAssembler(RegistryGeneration generation) noexcept
			: m_Generation(generation) {}

		[[nodiscard]] std::expected<
			std::shared_ptr<const RegistrySnapshot>,
			SyntheticRegistrySnapshotError>
		Build() {
			if (m_Generation == Aurore::Util::NoRegistryGeneration)
				return std::unexpected(SyntheticRegistrySnapshotError{
					.Code = SyntheticRegistrySnapshotErrorCode::InvalidGeneration,
					.Registry = std::nullopt,
					.Key = std::nullopt,
					.Cause = std::monostate{},
				});

			const auto keys = BuildKeys();
			if (!keys) return std::unexpected(keys.error());

			if (const auto result = DeclareBlocks(*keys); !result)
				return std::unexpected(result.error());
			if (const auto result = DeclareItems(*keys); !result)
				return std::unexpected(result.error());
			if (const auto result = DeclareBiomes(*keys); !result)
				return std::unexpected(result.error());
			if (const auto result = DeclareDimensionTypes(*keys); !result)
				return std::unexpected(result.error());
			if (const auto result = DeclareTags(*keys); !result)
				return std::unexpected(result.error());

			auto snapshot = std::move(m_Builder).Build(m_Generation, ValidateReferences);
			if (!snapshot)
				return std::unexpected(SyntheticRegistrySnapshotError{
					.Code = SyntheticRegistrySnapshotErrorCode::SnapshotBuildFailed,
					.Registry = snapshot.error().Registry,
					.Key = snapshot.error().Key,
					.Cause = snapshot.error(),
				});

			return std::move(*snapshot);
		}

	private:
		struct Keys final {
			ResourceLocation Block;
			ResourceLocation PolishedBlock;
			ResourceLocation Item;
			ResourceLocation PolishedItem;
			ResourceLocation Biome;
			ResourceLocation DimensionType;
			ResourceLocation BlocksTag;
			ResourceLocation ItemsTag;
			ResourceLocation BiomesTag;
			ResourceLocation DimensionTypesTag;
		};

		[[nodiscard]] static std::expected<ResourceLocation, SyntheticRegistrySnapshotError> ParseLocation(std::string_view value) {
			auto location = ResourceLocation::Parse(value);
			if (!location)
				return std::unexpected(SyntheticRegistrySnapshotError{
					.Code = SyntheticRegistrySnapshotErrorCode::InvalidResourceLocation,
					.Registry = std::nullopt,
					.Key = std::nullopt,
					.Cause = location.error(),
				});
			return std::move(*location);
		}

		[[nodiscard]] static std::expected<Keys, SyntheticRegistrySnapshotError> BuildKeys() {
			auto block = ParseLocation("aurore_test:block");
			if (!block) return std::unexpected(block.error());
			auto polished_block = ParseLocation("aurore_test:polished_block");
			if (!polished_block) return std::unexpected(polished_block.error());
			auto item = ParseLocation("aurore_test:item");
			if (!item) return std::unexpected(item.error());
			auto polished_item = ParseLocation("aurore_test:polished_item");
			if (!polished_item) return std::unexpected(polished_item.error());
			auto biome = ParseLocation("aurore_test:biome");
			if (!biome) return std::unexpected(biome.error());
			auto dimension_type = ParseLocation("aurore_test:dimension_type");
			if (!dimension_type) return std::unexpected(dimension_type.error());
			auto blocks_tag = ParseLocation("aurore_test:blocks");
			if (!blocks_tag) return std::unexpected(blocks_tag.error());
			auto items_tag = ParseLocation("aurore_test:items");
			if (!items_tag) return std::unexpected(items_tag.error());
			auto biomes_tag = ParseLocation("aurore_test:biomes");
			if (!biomes_tag) return std::unexpected(biomes_tag.error());
			auto dimension_types_tag = ParseLocation("aurore_test:dimension_types");
			if (!dimension_types_tag) return std::unexpected(dimension_types_tag.error());

			return Keys{
				.Block = std::move(*block),
				.PolishedBlock = std::move(*polished_block),
				.Item = std::move(*item),
				.PolishedItem = std::move(*polished_item),
				.Biome = std::move(*biome),
				.DimensionType = std::move(*dimension_type),
				.BlocksTag = std::move(*blocks_tag),
				.ItemsTag = std::move(*items_tag),
				.BiomesTag = std::move(*biomes_tag),
				.DimensionTypesTag = std::move(*dimension_types_tag),
			};
		}

		[[nodiscard]] static SyntheticRegistrySnapshotError RegistryDeclarationFailure(
			RegistryKind registry,
			const ResourceLocation& key,
			const Aurore::Util::RegistryDeclarationError& cause) {

			return SyntheticRegistrySnapshotError{
				.Code = SyntheticRegistrySnapshotErrorCode::RegistryDeclarationFailed,
				.Registry = registry,
				.Key = key,
				.Cause = cause,
			};
		}

		[[nodiscard]] static SyntheticRegistrySnapshotError TagDeclarationFailure(
			RegistryKind registry,
			const ResourceLocation& key,
			const Aurore::Util::RegistryTagDeclarationError& cause) {

			return SyntheticRegistrySnapshotError{
				.Code = SyntheticRegistrySnapshotErrorCode::TagDeclarationFailed,
				.Registry = registry,
				.Key = key,
				.Cause = cause,
			};
		}

		[[nodiscard]] std::expected<void, SyntheticRegistrySnapshotError> DeclareBlocks(const Keys& keys) {
			const auto block = m_Builder.Blocks().Declare(
				keys.Block,
				BlockDefinition{
					.Data = MakeBlockData(1, false),
				});
			if (!block)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::Block,
					keys.Block,
					block.error()));

			const auto polished_block = m_Builder.Blocks().Declare(
				keys.PolishedBlock,
				BlockDefinition{
					.Data = MakeBlockData(2, true),
				});
			if (!polished_block)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::Block,
					keys.PolishedBlock,
					polished_block.error()));

			return {};
		}

		[[nodiscard]] std::expected<void, SyntheticRegistrySnapshotError> DeclareItems(const Keys& keys) {
			const auto item = m_Builder.Items().Declare(
				keys.Item,
				ItemDefinition{
					.Data = MakeItemData(64, u"base", u"aurore_test:block"),
				});
			if (!item)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::Item,
					keys.Item,
					item.error()));

			const auto polished_item = m_Builder.Items().Declare(
				keys.PolishedItem,
				ItemDefinition{
					.Data = MakeItemData(64, u"polished", u"aurore_test:polished_block"),
				});
			if (!polished_item)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::Item,
					keys.PolishedItem,
					polished_item.error()));

			return {};
		}

		[[nodiscard]] std::expected<void, SyntheticRegistrySnapshotError> DeclareBiomes(const Keys& keys) {
			const auto biome = m_Builder.Biomes().Declare(
				keys.Biome,
				BiomeDefinition{
					.Data = MakeBiomeData(),
				});
			if (!biome)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::Biome,
					keys.Biome,
					biome.error()));

			return {};
		}

		[[nodiscard]] std::expected<void, SyntheticRegistrySnapshotError> DeclareDimensionTypes(const Keys& keys) {
			const auto dimension_type = m_Builder.DimensionTypes().Declare(
				keys.DimensionType,
				DimensionTypeDefinition{
					.Data = MakeDimensionTypeData(),
				});
			if (!dimension_type)
				return std::unexpected(RegistryDeclarationFailure(
					RegistryKind::DimensionType,
					keys.DimensionType,
					dimension_type.error()));

			return {};
		}

		[[nodiscard]] std::expected<void, SyntheticRegistrySnapshotError> DeclareTags(const Keys& keys) {
			const auto blocks = m_Builder.BlockTags().Declare(
				keys.BlocksTag,
				std::vector<ResourceLocation>{ keys.Block, keys.PolishedBlock });
			if (!blocks)
				return std::unexpected(TagDeclarationFailure(
					RegistryKind::Block,
					keys.BlocksTag,
					blocks.error()));

			const auto items = m_Builder.ItemTags().Declare(
				keys.ItemsTag,
				std::vector<ResourceLocation>{ keys.Item, keys.PolishedItem });
			if (!items)
				return std::unexpected(TagDeclarationFailure(
					RegistryKind::Item,
					keys.ItemsTag,
					items.error()));

			const auto biomes = m_Builder.BiomeTags().Declare(
				keys.BiomesTag,
				std::vector<ResourceLocation>{ keys.Biome });
			if (!biomes)
				return std::unexpected(TagDeclarationFailure(
					RegistryKind::Biome,
					keys.BiomesTag,
					biomes.error()));

			const auto dimension_types = m_Builder.DimensionTypeTags().Declare(
				keys.DimensionTypesTag,
				std::vector<ResourceLocation>{ keys.DimensionType });
			if (!dimension_types)
				return std::unexpected(TagDeclarationFailure(
					RegistryKind::DimensionType,
					keys.DimensionTypesTag,
					dimension_types.error()));

			return {};
		}

		[[nodiscard]] static std::expected<void, RegistrySnapshotError> ValidateReferences(
			const RegistrySnapshotDeclarationView& declarations) {

			const auto& items = declarations.GetItems();
			for (std::size_t index{ 0 }; index < items.Size(); ++index) {
				const auto& item = items.GetDeclarations()[index];
				const auto* placed_block_value = item.Value.Data.Find(u"placed_block");
				const auto* placed_block_text = placed_block_value == nullptr
					? nullptr
					: placed_block_value->AsString();

				if (placed_block_text == nullptr)
					return std::unexpected(RegistrySnapshotError{
						.Code = RegistrySnapshotErrorCode::InvalidDependency,
						.Registry = RegistryKind::Item,
						.Key = item.Key,
						.DeclarationIndex = index,
						.ReferencedRegistry = RegistryKind::Block,
						.ReferencedKey = std::nullopt,
					});

				const auto text = placed_block_text->ToUtf8();
				if (!text)
					return std::unexpected(RegistrySnapshotError{
						.Code = RegistrySnapshotErrorCode::InvalidDependency,
						.Registry = RegistryKind::Item,
						.Key = item.Key,
						.DeclarationIndex = index,
						.ReferencedRegistry = RegistryKind::Block,
						.ReferencedKey = std::nullopt,
					});

				const auto block_key = ResourceLocation::Parse(*text);
				if (!block_key)
					return std::unexpected(RegistrySnapshotError{
						.Code = RegistrySnapshotErrorCode::InvalidDependency,
						.Registry = RegistryKind::Item,
						.Key = item.Key,
						.DeclarationIndex = index,
						.ReferencedRegistry = RegistryKind::Block,
						.ReferencedKey = std::nullopt,
					});

				if (declarations.GetBlocks().Contains(*block_key)) continue;

				return std::unexpected(RegistrySnapshotError{
					.Code = RegistrySnapshotErrorCode::MissingReference,
					.Registry = RegistryKind::Item,
					.Key = item.Key,
					.DeclarationIndex = index,
					.ReferencedRegistry = RegistryKind::Block,
					.ReferencedKey = *block_key,
				});
			}

			return {};
		}

		[[nodiscard]] static NbtCompound MakeBlockData(
			std::int32_t state_count,
			bool polished) {

			NbtCompound data;
			data.Set(NbtString{ u"state_count" }, NbtValue::Int(state_count));
			data.Set(NbtString{ u"polished" }, NbtValue::Byte(polished ? 1 : 0));
			return data;
		}

		[[nodiscard]] static NbtCompound MakeItemData(
			std::int32_t maximum_stack_size,
			std::u16string_view variant,
			std::u16string_view placed_block) {

			NbtCompound data;
			data.Set(NbtString{ u"maximum_stack_size" }, NbtValue::Int(maximum_stack_size));
			data.Set(NbtString{ u"variant" }, NbtValue::String(NbtString{ std::u16string{ variant } }));
			data.Set(NbtString{ u"placed_block" }, NbtValue::String(NbtString{ std::u16string{ placed_block } }));
			return data;
		}

		[[nodiscard]] static NbtCompound MakeBiomeData() {
			NbtCompound data;
			data.Set(NbtString{ u"temperature" }, NbtValue::Float(0.8F));
			data.Set(NbtString{ u"downfall" }, NbtValue::Float(0.4F));
			return data;
		}

		[[nodiscard]] static NbtCompound MakeDimensionTypeData() {
			NbtCompound data;
			data.Set(NbtString{ u"natural" }, NbtValue::Byte(1));
			data.Set(NbtString{ u"coordinate_scale" }, NbtValue::Double(1.0));
			return data;
		}

		RegistryGeneration m_Generation;
		RegistrySnapshotBuilder m_Builder;
	};
}

namespace Aurore::Core {
	std::expected<
		std::shared_ptr<const Aurore::Util::RegistrySnapshot>,
		SyntheticRegistrySnapshotError>
	SyntheticRegistrySnapshotFactory::Build(Aurore::Util::RegistryGeneration generation) {
		return SyntheticRegistrySnapshotAssembler{ generation }.Build();
	}
}

