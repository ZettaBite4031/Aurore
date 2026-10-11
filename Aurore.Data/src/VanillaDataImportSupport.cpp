#include "VanillaDataImportSupport.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>

namespace {
	using Aurore::Data::MinecraftDataCorpus;
	using Aurore::Data::VanillaDataImportError;
	using Aurore::Data::VanillaDataImportErrorCode;
	using Aurore::Data::VanillaDataImportStage;
	using Aurore::Data::Detail::VanillaRegistryEntryContext;
	using Aurore::Data::Detail::VanillaRegistrySourceEntry;
	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::ResourceLocation;

	[[nodiscard]] bool IsIntegralNumber(double value) noexcept {
		return std::isfinite(value) && std::trunc(value) == value;
	}

	[[nodiscard]] std::string NormalizeRegistryPath(std::filesystem::path path) {
		path.replace_extension();
		return path.generic_string();
	}
}

namespace Aurore::Data::Detail {
	VanillaDataImportError MakeImportError(VanillaDataImportErrorCode code, const MinecraftDataCorpus& corpus, RegistryGeneration generation, VanillaDataImportStage stage) {
		return VanillaDataImportError{
			.Code = code,
			.Stage = stage,
			.MinecraftVersion = corpus.Version.Id,
			.ProtocolVersion = corpus.Version.ProtocolVersion,
			.Generation = generation,
		};
	}

	VanillaDataImportError MakeEntryError(VanillaDataImportErrorCode code, const VanillaRegistryEntryContext& context, std::string field, std::string detail) {
		auto error = MakeImportError(code, context.Corpus, context.Generation, context.Stage);
		error.Path = context.Entry.Path;
		error.Key = context.Entry.Key;
		error.Field = std::move(field);
		error.Detail = std::move(detail);
		return error;
	}

	std::expected<std::vector<VanillaRegistrySourceEntry>, VanillaDataImportError> DiscoverRegistryEntries(const MinecraftDataCorpus& corpus, RegistryGeneration generation, VanillaDataImportStage stage, const std::filesystem::path& relative_root) {
		const auto root = corpus.MinecraftDataRoot / relative_root;
		std::error_code error;

		const bool root_exists = std::filesystem::exists(root, error);
		if (error) {
			auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
			result.Path = root;
			result.SystemError = error;
			return std::unexpected(std::move(result));
		}
		if (!root_exists) {
			auto result = MakeImportError(VanillaDataImportErrorCode::MissingRegistrySource, corpus, generation, stage);
			result.Path = root;
			return std::unexpected(std::move(result));
		}

		error.clear();
		const bool root_is_directory = std::filesystem::is_directory(root, error);
		if (error) {
			auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
			result.Path = root;
			result.SystemError = error;
			return std::unexpected(std::move(result));
		}
		if (!root_is_directory) {
			auto result = MakeImportError(VanillaDataImportErrorCode::MissingRegistrySource, corpus, generation, stage);
			result.Path = root;
			return std::unexpected(std::move(result));
		}

		std::vector<VanillaRegistrySourceEntry> entries;
		std::filesystem::recursive_directory_iterator iterator{ root, error };
		const auto end = std::filesystem::recursive_directory_iterator{};
		if (error) {
			auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
			result.Path = root;
			result.SystemError = error;
			return std::unexpected(std::move(result));
		}

		while (iterator != end) {
			const auto path = iterator->path();

			error.clear();
			const bool is_file = iterator->is_regular_file(error);
			if (error) {
				auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
				result.Path = path;
				result.SystemError = error;
				return std::unexpected(std::move(result));
			}

			if (is_file && path.extension() == ".json") {
				error.clear();
				const auto relative = std::filesystem::relative(path, root, error);
				if (error) {
					auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
					result.Path = path;
					result.SystemError = error;
					return std::unexpected(std::move(result));
				}

				const auto resource_path = NormalizeRegistryPath(relative);
				auto key = ResourceLocation::FromParts("minecraft", resource_path);
				if (!key) {
					auto result = MakeImportError(VanillaDataImportErrorCode::InvalidResourceLocation, corpus, generation, stage);
					result.Path = path;
					result.Field = resource_path;
					result.Detail = "registry file path cannot be represented as a resource location";
					return std::unexpected(std::move(result));
				}

				entries.push_back(VanillaRegistrySourceEntry{
					.Key = std::move(*key),
					.Path = path,
				});
			}

			error.clear();
			iterator.increment(error);
			if (error) {
				auto result = MakeImportError(VanillaDataImportErrorCode::PathInspectionFailed, corpus, generation, stage);
				result.Path = path;
				result.SystemError = error;
				return std::unexpected(std::move(result));
			}
		}

		if (entries.empty()) {
			auto result = MakeImportError(VanillaDataImportErrorCode::EmptyRegistrySource, corpus, generation, stage);
			result.Path = root;
			return std::unexpected(std::move(result));
		}

		std::ranges::sort(entries, [](const VanillaRegistrySourceEntry& lhs, const VanillaRegistrySourceEntry& rhs) {
			return lhs.Key < rhs.Key;
		});

		return entries;
	}

	std::expected<Sonnet::value, VanillaDataImportError> ReadRegistryJson(const VanillaRegistryEntryContext& context) {
		std::error_code error;
		const auto size = std::filesystem::file_size(context.Entry.Path, error);
		if (error) {
			auto result = MakeEntryError(VanillaDataImportErrorCode::PathInspectionFailed, context);
			result.SystemError = error;
			return std::unexpected(std::move(result));
		}
		if (size > MaximumVanillaRegistryEntryBytes) {
			return std::unexpected(MakeEntryError(
				VanillaDataImportErrorCode::SourceEntryTooLarge,
				context,
				{},
				"registry JSON exceeds the per-entry import limit"));
		}

		std::ifstream stream{ context.Entry.Path, std::ios::binary };
		if (!stream.is_open())
			return std::unexpected(MakeEntryError(
				VanillaDataImportErrorCode::SourceReadFailed,
				context,
				{},
				"failed to open registry JSON"));

		std::string body(static_cast<std::size_t>(size), '\0');
		if (!body.empty()) {
			stream.read(body.data(), static_cast<std::streamsize>(body.size()));
			if (stream.gcount() != static_cast<std::streamsize>(body.size()))
				return std::unexpected(MakeEntryError(
					VanillaDataImportErrorCode::SourceReadFailed,
					context,
					{},
					"registry JSON changed or became unreadable during import"));
		}

		auto parsed = Sonnet::parse(body);
		if (!parsed)
			return std::unexpected(MakeEntryError(
				VanillaDataImportErrorCode::JsonParseFailed,
				context,
				{},
				parsed.error().msg));

		if (!parsed->is_object())
			return std::unexpected(MakeEntryError(
				VanillaDataImportErrorCode::InvalidJsonRoot,
				context,
				{},
				"registry entry root must be a JSON object"));

		return std::move(*parsed);
	}

	const Sonnet::value* FindField(const Sonnet::value& object, std::string_view field) noexcept {
		return object.is_object() ? object.find(field) : nullptr;
	}

	std::expected<const Sonnet::value*, VanillaDataImportError> ReadRequiredObject(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_object())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON object"));

		return value;
	}

	std::expected<bool, VanillaDataImportError> ReadRequiredBool(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_bool())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON boolean"));

		return value->as_bool();
	}

	std::expected<std::optional<bool>, VanillaDataImportError> ReadOptionalBool(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr) return std::optional<bool>{};

		if (!value->is_bool())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON boolean"));

		return std::optional<bool>{ value->as_bool() };
	}

	std::expected<std::int32_t, VanillaDataImportError> ReadRequiredInt(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context, std::int32_t minimum, std::int32_t maximum) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_number())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON number"));

		const double number = value->as_number();
		if (!IsIntegralNumber(number)
			|| number < static_cast<double>(minimum)
			|| number > static_cast<double>(maximum))
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field }, "integer value is outside the supported range"));

		return static_cast<std::int32_t>(number);
	}

	std::expected<float, VanillaDataImportError> ReadRequiredFloat(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_number())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON number"));

		const double number = value->as_number();
		if (!std::isfinite(number)
			|| number < -static_cast<double>(std::numeric_limits<float>::max())
			|| number > static_cast<double>(std::numeric_limits<float>::max()))
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field }, "floating-point value is not finite or representable"));

		return static_cast<float>(number);
	}

	std::expected<double, VanillaDataImportError> ReadRequiredDouble(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_number())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON number"));

		const double number = value->as_number();
		if (!std::isfinite(number))
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field }, "floating-point value must be finite"));

		return number;
	}

	std::expected<std::string, VanillaDataImportError> ReadRequiredString(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) {
		const auto* value = FindField(object, field);
		if (value == nullptr)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::MissingField, context, std::string{ field }));

		if (!value->is_string())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON string"));

		return std::string{ value->as_string() };
	}

	std::expected<std::optional<std::string>, VanillaDataImportError> 	ReadOptionalString(const Sonnet::value& object, std::string_view field, const VanillaRegistryEntryContext& context) { 
		const auto* value = FindField(object, field);
		if (value == nullptr) return std::optional<std::string>{};

		if (!value->is_string())
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldType, context, std::string{ field }, "expected JSON string"));

		return std::optional<std::string>{ std::string{ value->as_string() } };
	}

	std::expected<Aurore::Util::NbtString, VanillaDataImportError> MakeNbtString(std::string_view value, std::string_view field, const VanillaRegistryEntryContext& context) {
		auto result = Aurore::Util::NbtString::FromUtf8(value);
		if (!result)
			return std::unexpected(MakeEntryError(VanillaDataImportErrorCode::InvalidFieldValue, context, std::string{ field }, "value is not valid UTF-8"));

		return std::move(*result);
	}
}
