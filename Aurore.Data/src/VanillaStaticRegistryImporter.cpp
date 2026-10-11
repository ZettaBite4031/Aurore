#include "VanillaDataImportSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
	using Aurore::Data::MinecraftDataCorpus;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Util::BlockDefinition;
	using Aurore::Util::ItemDefinition;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistryRuntimeId;
	using Aurore::Util::RegistrySnapshotBuilder;
	using Aurore::Util::ResourceLocation;

	inline constexpr std::uintmax_t MaximumRegistryReportBytes{ 64ull * 1024ull * 1024ull };

	enum class StaticRegistryKind : std::uint8_t {
		Block,
		Item,
		Generic,
	};

	struct StaticRegistryDescriptor final {
		std::string_view Key;
		StaticRegistryKind Kind;
	};

	struct StaticRegistryEntry final {
		ResourceLocation Key;
		RegistryRuntimeId ProtocolId;
	};

	constexpr StaticRegistryDescriptor StaticRegistries[]{
		{ "minecraft:block", StaticRegistryKind::Block },
		{ "minecraft:item", StaticRegistryKind::Item },
		{ "minecraft:entity_type", StaticRegistryKind::Generic },
		{ "minecraft:fluid", StaticRegistryKind::Generic },
		{ "minecraft:game_event", StaticRegistryKind::Generic },
		{ "minecraft:point_of_interest_type", StaticRegistryKind::Generic },
		{ "minecraft:potion", StaticRegistryKind::Generic },
	};

	[[nodiscard]] VanillaDataImportError MakeReportError(VanillaDataImportErrorCode code, const MinecraftDataCorpus& corpus, RegistryGeneration generation, std::string field = {}, std::string detail = {}) {
		auto error = Aurore::Data::Detail::MakeImportError(code, corpus, generation, VanillaDataImportStage::StaticRegistries);
		error.Path = corpus.RegistryReport;
		error.Field = std::move(field);
		error.Detail = std::move(detail);
		return error;
	}

	[[nodiscard]] std::expected<Sonnet::value, VanillaDataImportError> ReadRegistryReport(const MinecraftDataCorpus& corpus, RegistryGeneration generation) {

		std::error_code error;
		const auto size = std::filesystem::file_size(corpus.RegistryReport, error);
		if (error) {
			auto result = MakeReportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation);
			result.SystemError = error;
			return std::unexpected(std::move(result));
		}

		if (size > MaximumRegistryReportBytes)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::SourceEntryTooLarge, corpus, generation, {},
				"registry report exceeds the import size limit"));

		std::ifstream stream{ corpus.RegistryReport, std::ios::binary };
		if (!stream.is_open())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::SourceReadFailed, corpus, generation, {},
				"failed to open registry report"));

		std::string body(static_cast<std::size_t>(size), '\0');
		if (!body.empty()) {
			stream.read(body.data(), static_cast<std::streamsize>(body.size()));
			if (stream.gcount() != static_cast<std::streamsize>(body.size()))
				return std::unexpected(MakeReportError(VanillaDataImportErrorCode::SourceReadFailed, corpus, generation, {},
					"registry report changed or became unreadable during import"));
		}

		auto parsed = Sonnet::parse(body);
		if (!parsed)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::JsonParseFailed, corpus, generation, {},
				parsed.error().msg));

		if (!parsed->is_object())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidJsonRoot, corpus, generation, {},
				"registry report root must be a JSON object"));

		return std::move(*parsed);
	}

	[[nodiscard]] std::expected<RegistryRuntimeId, VanillaDataImportError> ReadProtocolId(const Sonnet::value& entry, std::string_view registry_key, std::string_view entry_key, const MinecraftDataCorpus& corpus, RegistryGeneration generation) {
		const std::string field = std::string{ registry_key } + ".entries." + std::string{ entry_key } + ".protocol_id";
		if (!entry.is_object())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldType, corpus, generation, field,
				"registry entry must be a JSON object"));

		const auto* protocol_id = Aurore::Data::Detail::FindField(entry, "protocol_id");
		if (protocol_id == nullptr)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::MissingField, corpus, generation, field));

		if (!protocol_id->is_number())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldType, corpus, generation, field,
				"protocol_id must be a JSON number"));

		const double number = protocol_id->as_number();
		if (!std::isfinite(number)
			|| std::trunc(number) != number
			|| number < 0.0
			|| number > static_cast<double>(std::numeric_limits<RegistryRuntimeId>::max()))
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldValue, corpus, generation, field,
				"protocol_id must be a non-negative registry runtime ID"));

		return static_cast<RegistryRuntimeId>(number);
	}

	[[nodiscard]] std::expected<std::vector<StaticRegistryEntry>, VanillaDataImportError> ReadStaticRegistryEntries(const Sonnet::value& report, const StaticRegistryDescriptor& descriptor, const MinecraftDataCorpus& corpus, RegistryGeneration generation) {
		const auto* registry = Aurore::Data::Detail::FindField(report, descriptor.Key);
		if (registry == nullptr)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::MissingField, corpus, generation, std::string{ descriptor.Key },
				"required static registry is missing from registries.json"));

		if (!registry->is_object())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldType, corpus, generation, std::string{ descriptor.Key },
				"registry report entry must be a JSON object"));

		const auto* entries = Aurore::Data::Detail::FindField(*registry, "entries");
		if (entries == nullptr)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::MissingField, corpus, generation,
				std::string{ descriptor.Key } + ".entries"));

		if (!entries->is_object())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldType, corpus, generation, std::string{ descriptor.Key } + ".entries",
				"registry entries must be a JSON object"));

		std::vector<StaticRegistryEntry> result;
		result.reserve(entries->as_object().size());

		for (const auto& [name, value] : entries->as_object()) {
			const std::string_view entry_name{ name.data(), name.size() };

			auto key = ResourceLocation::Parse(entry_name);
			if (!key)
				return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidResourceLocation, corpus, generation, std::string{ descriptor.Key } + ".entries." + std::string{ entry_name },
					"registry report contains an invalid resource location"));

			auto protocol_id = ReadProtocolId(value, descriptor.Key, entry_name, corpus, generation);
			if (!protocol_id) return std::unexpected(protocol_id.error());

			result.push_back(StaticRegistryEntry{ .Key = std::move(*key), .ProtocolId = *protocol_id, });
		}

		if (result.empty())
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::EmptyRegistrySource, corpus, generation, std::string{ descriptor.Key } + ".entries",
				"required static registry contains no entries"));

		std::ranges::sort(result, [](const StaticRegistryEntry& lhs, const StaticRegistryEntry& rhs) {
			return lhs.ProtocolId < rhs.ProtocolId;
		});

		for (std::size_t index{ 0 }; index < result.size(); ++index) {
			if (result[index].ProtocolId == index) continue;

			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidFieldValue, corpus, generation,
				std::string{ descriptor.Key } + ".entries." + result[index].Key.ToString() + ".protocol_id",
				"protocol IDs must form a contiguous zero-based sequence"));
		}

		return result;
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> DeclareTypedStaticRegistry(const StaticRegistryDescriptor& descriptor, std::vector<StaticRegistryEntry> entries, const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		for (auto& entry : entries) {
			std::expected<void, Aurore::Util::RegistryDeclarationError> declaration;

			if (descriptor.Kind == StaticRegistryKind::Block)
				declaration = builder.Blocks().Declare(entry.Key, BlockDefinition{});
			else declaration = builder.Items().Declare(entry.Key, ItemDefinition{});
			if (declaration) continue;

			auto error = MakeReportError(VanillaDataImportErrorCode::RegistryDeclarationFailed, corpus, generation,
				std::string{ descriptor.Key } + ".entries." + entry.Key.ToString());
			error.Key = entry.Key;
			error.DeclarationError = declaration.error();
			return std::unexpected(std::move(error));
		}

		return {};
	}

	[[nodiscard]] std::expected<void, VanillaDataImportError> DeclareGenericStaticRegistry(const StaticRegistryDescriptor& descriptor, std::vector<StaticRegistryEntry> entries, const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		auto registry_key = ResourceLocation::Parse(descriptor.Key);
		if (!registry_key)
			return std::unexpected(MakeReportError(VanillaDataImportErrorCode::InvalidResourceLocation, corpus, generation, std::string{ descriptor.Key },
				"built-in static registry descriptor contains an invalid key"));

		auto registry = builder.StaticRegistries().CreateRegistryBuilder(*registry_key);
		for (auto& entry : entries) {
			const auto declaration = registry.Declare(entry.Key);
			if (declaration) continue;

			auto error = MakeReportError(VanillaDataImportErrorCode::RegistryDeclarationFailed, corpus, generation,
				std::string{ descriptor.Key } + ".entries." + entry.Key.ToString());
			error.Key = entry.Key;
			error.DeclarationError = declaration.error();
			return std::unexpected(std::move(error));
		}

		auto built = std::move(registry).Build();
		if (!built) {
			auto error = MakeReportError(VanillaDataImportErrorCode::StaticRegistryBuildFailed, corpus, generation, std::string{ descriptor.Key });
			error.Key = *registry_key;
			error.StaticRegistryBuildError = built.error();
			return std::unexpected(std::move(error));
		}

		const auto collection = builder.StaticRegistries().Declare(std::move(*built));
		if (!collection) {
			auto error = MakeReportError(VanillaDataImportErrorCode::StaticRegistryCollectionDeclarationFailed, corpus, generation, std::string{ descriptor.Key });
			error.Key = *registry_key;
			error.StaticRegistryCollectionError = collection.error();
			return std::unexpected(std::move(error));
		}

		return {};
	}
}

namespace Aurore::Data::Detail {
	std::expected<void, VanillaDataImportError> ImportStaticRegistries(const MinecraftDataCorpus& corpus, RegistryGeneration generation, RegistrySnapshotBuilder& builder) {
		auto report = ReadRegistryReport(corpus, generation);
		if (!report) return std::unexpected(report.error());

		for (const auto& descriptor : StaticRegistries) {
			auto entries = ReadStaticRegistryEntries(*report, descriptor, corpus, generation);
			if (!entries) return std::unexpected(entries.error());

			std::expected<void, VanillaDataImportError> declaration;
			if (descriptor.Kind == StaticRegistryKind::Generic)
				declaration = DeclareGenericStaticRegistry(descriptor, std::move(*entries), corpus, generation, builder);
			else declaration = DeclareTypedStaticRegistry(descriptor, std::move(*entries), corpus, generation, builder);
			if (!declaration) return std::unexpected(declaration.error());
		}

		return {};
	}
}
