#include <Aurore/Util/ResourceLocation.hpp>

#include <utility>

namespace {
	using Aurore::Util::ResourceLocationError;
	using Aurore::Util::ResourceLocationErrorCode;

	constexpr bool IsNamespaceCharacter(char character) noexcept {
		return (character >= 'a' && character <= 'z')
			|| (character >= '0' && character <= '9')
			|| character == '_' || character == '-'
			|| character == '.';
	}

	constexpr bool IsPathCharacter(char character) noexcept {
		return IsNamespaceCharacter(character) || character == '/';
	}

	std::expected<void, ResourceLocationError> ValidateNamespace(std::string_view namespace_name, std::size_t base_offset) noexcept {
		if (namespace_name.empty())
			return std::unexpected(ResourceLocationError{
			.Code = ResourceLocationErrorCode::EmptyNamespace,
			.Offset = base_offset,
			});

		for (std::size_t index{ 0 }; index < namespace_name.size(); index++) {
			if (IsNamespaceCharacter(namespace_name[index])) continue;
			return std::unexpected(ResourceLocationError{
				.Code = ResourceLocationErrorCode::InvalidNamespaceCharacter,
				.Offset = base_offset + index,
			});
		}

		return {};
	}

	std::expected<void, ResourceLocationError> ValidatePath(std::string_view path, std::size_t base_offset) noexcept {
		if (path.empty()) {
			return std::unexpected(ResourceLocationError{
				.Code = ResourceLocationErrorCode::EmptyPath,
				.Offset = base_offset,
			});
		}

		for (std::size_t index{ 0 }; index < path.size(); ++index) {
			if (IsPathCharacter(path[index])) continue;
			return std::unexpected(ResourceLocationError{
				.Code = ResourceLocationErrorCode::InvalidPathCharacter,
				.Offset = base_offset + index,
			});
		}

		return {};
	}
}

namespace Aurore::Util {
	ResourceLocation::ResourceLocation(std::string namespace_name, std::string path) noexcept
		: m_Namespace(std::move(namespace_name)), m_Path(std::move(path)) {}

	std::expected<ResourceLocation, ResourceLocationError> ResourceLocation::Parse(std::string_view value) {
		if (value.empty())
			return std::unexpected(ResourceLocationError{
				.Code = ResourceLocationErrorCode::EmptyValue,
				.Offset = 0,
			});

		const auto separator = value.find(':');
		if (separator == std::string_view::npos) {
			const auto path_result = ValidatePath(value, 0);
			if (!path_result)
				return std::unexpected(path_result.error());

			return ResourceLocation{
				std::string{ DefaultNamespace },
				std::string{ value },
			};
		}

		const auto namespace_name = value.substr(0, separator);
		const auto path = value.substr(separator + 1);

		const auto namespace_result = ValidateNamespace(namespace_name, 0);
		if (!namespace_result)
			return std::unexpected(namespace_result.error());

		const auto additional_separator = value.find(':', separator + 1);
		if (additional_separator != std::string_view::npos)
			return std::unexpected(ResourceLocationError{
				.Code = ResourceLocationErrorCode::MultipleSeparators,
				.Offset = additional_separator,
			});

		const auto path_result = ValidatePath(path, separator + 1);
		if (!path_result)
			return std::unexpected(path_result.error());

		return ResourceLocation{
			std::string{ namespace_name },
			std::string{ path },
		};
	}

	std::expected<ResourceLocation, ResourceLocationError> ResourceLocation::FromParts(std::string_view namespace_name, std::string_view path) {
		const auto namespace_result = ValidateNamespace(namespace_name, 0);
		if (!namespace_result)
			return std::unexpected(namespace_result.error());

		const auto path_result = ValidatePath(path, 0);
		if (!path_result)
			return std::unexpected(path_result.error());

		return ResourceLocation{
			std::string{ namespace_name },
			std::string{ path },
		};
	}

	std::string ResourceLocation::ToString() const {
		std::string result{ m_Namespace };
		result.push_back(':');
		result.append(m_Path);
		return result;
	}
}
