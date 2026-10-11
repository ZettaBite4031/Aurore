#include "VanillaDataImportSupport.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
	using Aurore::Data::MinecraftDataCorpus;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Data::Detail::VanillaRegistryEntryContext;
	using Aurore::Data::Detail::VanillaRegistrySourceEntry;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistrySnapshotBuilder;
	using Aurore::Util::ResourceLocation;

	enum class TagRegistryTarget : std::uint8_t {
		Block,
		Item,
		Biome,
		DimensionType,
		Network,
		Static,
	};

	struct TagRegistryDescriptor final {
		std::string_view RegistryKey;
		std::string_view RegistryPath;
		TagRegistryTarget Target;
	};

	struct VanillaTagReference final {
		ResourceLocation Key;
		bool IsTag{ false };
		bool Required{ true };
	};

	struct VanillaTagDefinition final {
		ResourceLocation Key;
		std::filesystem::path Path;
		std::vector<VanillaTagReference> Values;
	};

	struct ResolvedVanillaTag final {
		ResourceLocation Key;
		std::vector<ResourceLocation> Members;
	};

	constexpr std::array TagRegistries{
		TagRegistryDescriptor{ "minecraft:block", "block", TagRegistryTarget::Block },
		TagRegistryDescriptor{ "minecraft:item", "item", TagRegistryTarget::Item },
		TagRegistryDescriptor{ "minecraft:dimension_type", "dimension_type", TagRegistryTarget::DimensionType },
		TagRegistryDescriptor{ "minecraft:worldgen/biome", "worldgen/biome", TagRegistryTarget::Biome },

		TagRegistryDescriptor{ "minecraft:chat_type", "chat_type", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:trim_pattern", "trim_pattern", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:trim_material", "trim_material", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:wolf_variant", "wolf_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:wolf_sound_variant", "wolf_sound_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:pig_variant", "pig_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:frog_variant", "frog_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:cat_variant", "cat_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:cow_variant", "cow_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:chicken_variant", "chicken_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:zombie_nautilus_variant", "zombie_nautilus_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:painting_variant", "painting_variant", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:damage_type", "damage_type", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:banner_pattern", "banner_pattern", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:enchantment", "enchantment", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:jukebox_song", "jukebox_song", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:instrument", "instrument", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:test_environment", "test_environment", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:test_instance", "test_instance", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:dialog", "dialog", TagRegistryTarget::Network },
		TagRegistryDescriptor{ "minecraft:timeline", "timeline", TagRegistryTarget::Network },

		TagRegistryDescriptor{ "minecraft:entity_type", "entity_type", TagRegistryTarget::Static },
		TagRegistryDescriptor{ "minecraft:fluid", "fluid", TagRegistryTarget::Static },
		TagRegistryDescriptor{ "minecraft:game_event", "game_event", TagRegistryTarget::Static },
		TagRegistryDescriptor{ "minecraft:point_of_interest_type", "point_of_interest_type", TagRegistryTarget::Static },
		TagRegistryDescriptor{ "minecraft:potion", "potion", TagRegistryTarget::Static },
	};

	[[nodiscard]] VanillaDataImportError MakeTagError(VanillaDataImportErrorCode code, const MinecraftDataCorpus& corpus, RegistryGeneration generation, const std::filesystem::path& path, std::optional<ResourceLocation> key = std::nullopt, std::string field = {}, std::string detail = {}) {
		auto error = Aurore::Data::Detail::MakeImportError(code, corpus, generation, VanillaDataImportStage::Tags);
		error.Path = path;
		error.Key = std::move(key);
		error.Field = std::move(field);
		error.Detail = std::move(detail);
		return error;
	}

	[[nodiscard]] std::expected<ResourceLocation, VanillaDataImportError> ParseTagReferenceLocation(std::string_view value, const VanillaRegistryEntryContext& context, std::string_view field, bool& is_tag) {
		is_tag = !value.empty() && value.front() == '#';
		if (is_tag) value.remove_prefix(1);

		auto key = ResourceLocation::Parse(value);
		if (key) return std::move(*key);

		return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidResourceLocation, context, std::string{ field },
				"tag reference is not a valid resource location"));
	}

	[[nodiscard]] std::expected<VanillaTagReference, VanillaDataImportError> ParseTagReference(const Sonnet::value& value, std::size_t index, const VanillaRegistryEntryContext& context) {
		const std::string field = "values[" + std::to_string(index) + "]";

		if (value.is_string()) {
			const std::string_view raw{ value.as_string().data(), value.as_string().size() };
			bool is_tag{ false };
			auto key = ParseTagReferenceLocation(raw, context, field, is_tag);
			if (!key) return std::unexpected(key.error());

			return VanillaTagReference{
				.Key = std::move(*key),
				.IsTag = is_tag,
				.Required = true,
			};
		}

		if (!value.is_object())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, field,
					"tag value must be a string or object"));

		constexpr std::array<std::string_view, 2> AllowedFields{ "id", "required", };
		const auto fields = Aurore::Data::Detail::ValidateObjectFields(value, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		auto id = Aurore::Data::Detail::ReadRequiredString(value, "id", context);
		if (!id) return std::unexpected(id.error());

		auto required = Aurore::Data::Detail::ReadOptionalBool(value, "required", context);
		if (!required) return std::unexpected(required.error());

		bool is_tag{ false };
		auto key = ParseTagReferenceLocation(*id, context, field + ".id", is_tag);
		if (!key) return std::unexpected(key.error());

		return VanillaTagReference{
			.Key = std::move(*key),
			.IsTag = is_tag,
			.Required = required->value_or(true),
		};
	}

	[[nodiscard]] std::expected<VanillaTagDefinition, VanillaDataImportError> ParseTagDefinition(const MinecraftDataCorpus& corpus, RegistryGeneration generation, const VanillaRegistrySourceEntry& source) {
		const VanillaRegistryEntryContext context{
			.Corpus = corpus,
			.Generation = generation,
			.Stage = VanillaDataImportStage::Tags,
			.Entry = source,
		};

		auto root = Aurore::Data::Detail::ReadRegistryJson(context);
		if (!root) return std::unexpected(root.error());

		constexpr std::array<std::string_view, 2> AllowedFields{ "values", "replace", };

		const auto fields = Aurore::Data::Detail::ValidateObjectFields(*root, AllowedFields, context);
		if (!fields) return std::unexpected(fields.error());

		const auto* values = Aurore::Data::Detail::FindField(*root, "values");
		if (values == nullptr)
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::MissingField, context, "values"));

		if (!values->is_array())
			return std::unexpected(Aurore::Data::Detail::MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, "values",
					"tag values must be a JSON array"));

		auto replace = Aurore::Data::Detail::ReadOptionalBool(*root, "replace", context);
		if (!replace) return std::unexpected(replace.error());

		/*
			The generated vanilla corpus contains a single resource stack,
			so replace has no merge effect here. It is still parsed and
			type-checked to keep the source contract honest.
		*/
		(void)replace;
		VanillaTagDefinition definition{ .Key = source.Key, .Path = source.Path, };
		definition.Values.reserve(values->as_array().size());
		for (std::size_t index{ 0 }; index < values->as_array().size(); ++index) {
			auto reference = ParseTagReference(values->as_array()[index], index, context);
			if (!reference) return std::unexpected(reference.error());
			definition.Values.push_back(std::move(*reference));
		}

		return definition;
	}

	[[nodiscard]] std::expected<std::vector<VanillaTagDefinition>, VanillaDataImportError> LoadTagDefinitions(const TagRegistryDescriptor& descriptor, const MinecraftDataCorpus& corpus, RegistryGeneration generation) {
		const auto root = std::filesystem::path{ "tags" } / std::filesystem::path{ descriptor.RegistryPath };
		auto sources = Aurore::Data::Detail::DiscoverRegistryEntries(corpus, generation, VanillaDataImportStage::Tags, root);
		if (!sources) {
			if (sources.error().Code == VanillaDataImportErrorCode::MissingRegistrySource
				|| sources.error().Code == VanillaDataImportErrorCode::EmptyRegistrySource)
				return std::vector<VanillaTagDefinition>{};
			return std::unexpected(sources.error());
		}

		std::vector<VanillaTagDefinition> result;
		result.reserve(sources->size());
		for (const auto& source : *sources) {
			auto definition = ParseTagDefinition(corpus, generation, source);
			if (!definition) return std::unexpected(definition.error());
			result.push_back(std::move(*definition));
		}

		return result;
	}

	template<typename Contains>
	[[nodiscard]] std::expected<std::vector<ResolvedVanillaTag>, VanillaDataImportError> ResolveTags(const MinecraftDataCorpus& corpus, RegistryGeneration generation, const std::vector<VanillaTagDefinition>& definitions, Contains&& contains) {
		enum class VisitState : std::uint8_t {
			Unvisited,
			Visiting,
			Resolved,
		};

		std::unordered_map<ResourceLocation, std::size_t> indices;
		indices.reserve(definitions.size());

		for (std::size_t index{ 0 }; index < definitions.size(); ++index)
			indices.emplace(definitions[index].Key, index);

		std::vector<VisitState> states(definitions.size(), VisitState::Unvisited);
		std::vector<std::vector<ResourceLocation>> resolved(definitions.size());
		std::vector<ResourceLocation> stack;

		std::function<std::expected<void, VanillaDataImportError>(std::size_t)> resolve_one;

		resolve_one = [&](std::size_t index) -> std::expected<void, VanillaDataImportError> {
			if (states[index] == VisitState::Resolved) return {};

			const auto& definition = definitions[index];

			if (states[index] == VisitState::Visiting) {
				std::string detail{ "cyclic tag reference: " };
				for (const auto& key : stack) {
					if (detail.back() != ' ') detail.append(" -> ");
					detail.append(key.ToString());
				}
				if (!stack.empty()) detail.append(" -> ");
				detail.append(definition.Key.ToString());

				return std::unexpected(MakeTagError(VanillaDataImportErrorCode::CyclicTagReference, corpus, generation, definition.Path, definition.Key, definition.Key.ToString(), std::move(detail)));
			}

			states[index] = VisitState::Visiting;
			stack.push_back(definition.Key);

			std::unordered_set<ResourceLocation> seen;
			std::vector<ResourceLocation> members;

			for (const auto& reference : definition.Values) {
				if (reference.IsTag) {
					const auto nested = indices.find(reference.Key);
					if (nested == indices.end()) {
						if (!reference.Required) continue;

						stack.pop_back();
						states[index] = VisitState::Unvisited;
						return std::unexpected(MakeTagError(VanillaDataImportErrorCode::MissingTagReference, corpus, generation, definition.Path, definition.Key, reference.Key.ToString(),
							"required nested tag does not exist"));
					}

					const auto nested_result = resolve_one(nested->second);
					if (!nested_result) {
						stack.pop_back();
						states[index] = VisitState::Unvisited;
						return std::unexpected(nested_result.error());
					}

					for (const auto& member : resolved[nested->second])
						if (seen.emplace(member).second)
							members.push_back(member);

					continue;
				}

				if (!contains(reference.Key)) {
					if (!reference.Required) continue;

					stack.pop_back();
					states[index] = VisitState::Unvisited;
					return std::unexpected(MakeTagError(VanillaDataImportErrorCode::MissingRegistryMember, corpus, generation, definition.Path, definition.Key, reference.Key.ToString(),
						"required tag member is not present in the target registry"));
				}

				if (seen.emplace(reference.Key).second)
					members.push_back(reference.Key);
			}

			resolved[index] = std::move(members);
			states[index] = VisitState::Resolved;
			stack.pop_back();
			return {};
		};

		for (std::size_t index{ 0 }; index < definitions.size(); ++index) {
			const auto result = resolve_one(index);
			if (!result) return std::unexpected(result.error());
		}

		std::vector<ResolvedVanillaTag> result;
		result.reserve(definitions.size());

		for (std::size_t index{ 0 }; index < definitions.size(); ++index)
			result.push_back(ResolvedVanillaTag{ .Key = definitions[index].Key, .Members = std::move(resolved[index]), });
		return result;
	}

	template<typename Builder>
	[[nodiscard]] std::expected<void, VanillaDataImportError> DeclareTypedTags(const TagRegistryDescriptor& descriptor, const MinecraftDataCorpus& corpus, RegistryGeneration generation, std::vector<ResolvedVanillaTag> tags, Builder& target) {
		for (auto& tag : tags) {
			const auto declaration = target.Declare(tag.Key, std::move(tag.Members));
			if (declaration) continue;

			auto error = MakeTagError(VanillaDataImportErrorCode::TagDeclarationFailed, corpus, generation, corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, tag.Key);
			error.TagDeclarationError = declaration.error();
			return std::unexpected(std::move(error));
		}

		return {};
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> ImportTagRegistry(const TagRegistryDescriptor& descriptor, const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		auto definitions = LoadTagDefinitions(descriptor, corpus, generation);
		if (!definitions) return std::unexpected(definitions.error());
		if (definitions->empty()) return {};

		auto registry_key = ResourceLocation::Parse(descriptor.RegistryKey);
		if (!registry_key)
			return std::unexpected(MakeTagError(VanillaDataImportErrorCode::InvalidResourceLocation, corpus, generation,
				corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath,
				std::nullopt, std::string{ descriptor.RegistryKey },
				"built-in tag registry descriptor has an invalid registry key"));

		switch (descriptor.Target) {
		case TagRegistryTarget::Block: {
			auto resolved = ResolveTags(corpus, generation, *definitions, [&builder](const ResourceLocation& key) { return builder.Blocks().Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());
			return DeclareTypedTags(descriptor, corpus, generation, std::move(*resolved), builder.BlockTags());
		}

		case TagRegistryTarget::Item: {
			auto resolved = ResolveTags(corpus, generation, *definitions, [&builder](const ResourceLocation& key) { return builder.Items().Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());
			return DeclareTypedTags(descriptor, corpus, generation, std::move(*resolved), builder.ItemTags());
		}

		case TagRegistryTarget::Biome: {
			auto resolved = ResolveTags(corpus, generation, *definitions, [&builder](const ResourceLocation& key) { return builder.Biomes().Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());
			return DeclareTypedTags(descriptor, corpus, generation, std::move(*resolved), builder.BiomeTags());
		}

		case TagRegistryTarget::DimensionType: {
			auto resolved = ResolveTags(corpus, generation, *definitions, [&builder](const ResourceLocation& key) { return builder.DimensionTypes().Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());
			return DeclareTypedTags(descriptor, corpus, generation, std::move(*resolved), builder.DimensionTypeTags());
		}

		case TagRegistryTarget::Network: {
			const auto* registry = builder.NetworkRegistries().Find(*registry_key);
			if (registry == nullptr)
				return std::unexpected(MakeTagError(VanillaDataImportErrorCode::MissingRegistrySource, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath,
					*registry_key, std::string{ descriptor.RegistryKey },
					"tag registry is not present in the synchronized registry snapshot"));

			auto resolved = ResolveTags(corpus, generation, *definitions, [registry](const ResourceLocation& key) { return registry->Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());

			auto section = builder.NetworkRegistryTags().CreateSectionBuilder(*registry_key);
			for (auto& tag : *resolved) {
				const auto declaration = section.Declare(tag.Key, std::move(tag.Members));
				if (declaration) continue;

				auto error = MakeTagError(VanillaDataImportErrorCode::TagDeclarationFailed, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, tag.Key);

				error.TagDeclarationError = declaration.error();
				return std::unexpected(std::move(error));
			}

			const auto declaration = builder.NetworkRegistryTags().Declare(std::move(section));
			if (!declaration) {
				auto error = MakeTagError(VanillaDataImportErrorCode::NetworkRegistryTagCollectionDeclarationFailed, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, *registry_key);

				error.NetworkRegistryTagCollectionError = declaration.error();
				return std::unexpected(std::move(error));
			}

			return {};
		}

		case TagRegistryTarget::Static: {
			const auto* registry = builder.StaticRegistries().Find(*registry_key);
			if (registry == nullptr)
				return std::unexpected(MakeTagError(VanillaDataImportErrorCode::MissingRegistrySource, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, *registry_key, std::string{ descriptor.RegistryKey },
					"tag registry is not present in the static identity snapshot"));

			auto resolved = ResolveTags(corpus, generation, *definitions, [registry](const ResourceLocation& key) { return registry->Contains(key); });
			if (!resolved) return std::unexpected(resolved.error());

			auto section = builder.StaticRegistryTags().CreateSectionBuilder(*registry_key);
			for (auto& tag : *resolved) {
				const auto declaration = section.Declare(tag.Key, std::move(tag.Members));
				if (declaration) continue;

				auto error = MakeTagError(VanillaDataImportErrorCode::TagDeclarationFailed, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, tag.Key);

				error.TagDeclarationError = declaration.error();
				return std::unexpected(std::move(error));
			}

			const auto declaration = builder.StaticRegistryTags().Declare(std::move(section));
			if (!declaration) {
				auto error = MakeTagError(VanillaDataImportErrorCode::StaticRegistryTagCollectionDeclarationFailed, corpus, generation,
					corpus.MinecraftDataRoot / "tags" / descriptor.RegistryPath, *registry_key);

				error.StaticRegistryTagCollectionError = declaration.error();
				return std::unexpected(std::move(error));
			}

			return {};
		}
		}

		std::unreachable();
	}
}

namespace Aurore::Data::Detail {
	std::expected<void, VanillaDataImportError> ImportTags(const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		for (const auto& descriptor : TagRegistries) {
			const auto result = ImportTagRegistry(descriptor, corpus, generation, builder);
			if (!result) return std::unexpected(result.error());
		}
		return {};
	}
}
