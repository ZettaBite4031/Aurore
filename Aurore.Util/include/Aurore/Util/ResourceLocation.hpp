#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace Aurore::Util {
	enum class ResourceLocationErrorCode : std::uint8_t {
		EmptyValue,
		EmptyNamespace,
		EmptyPath,
		MultipleSeparators,
		InvalidNamespaceCharacter,
		InvalidPathCharacter,
	};

	struct ResourceLocationError final {
		ResourceLocationErrorCode Code;
		std::size_t Offset{ 0 };

		auto operator<=>(const ResourceLocationError&) const noexcept = default;
	};

	class ResourceLocation final {
	public:
		static constexpr std::string_view DefaultNamespace{ "minecraft" };

		// Parse error offsets refer to byte positions in value.
		[[nodiscard]] static std::expected<ResourceLocation, ResourceLocationError> Parse(std::string_view value);
		[[nodiscard]] static std::expected<ResourceLocation, ResourceLocationError> FromParts(std::string_view namespace_name, std::string_view path);

		[[nodiscard]] std::string_view GetNamespace() const noexcept { return m_Namespace; }
		[[nodiscard]] std::string_view GetPath() const noexcept { return m_Path; }

		[[nodiscard]] std::string ToString() const;

		auto operator<=>(const ResourceLocation&) const noexcept = default;

	private:
		ResourceLocation(std::string namespace_name, std::string path) noexcept;

		std::string m_Namespace;
		std::string m_Path;
	};
}

template<>
struct std::hash<Aurore::Util::ResourceLocation> {
	std::size_t operator()(const Aurore::Util::ResourceLocation& value) const noexcept {
		std::size_t result;
		std::size_t prime;

		if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
			result = static_cast<std::size_t>(14695981039346656037ull);
			prime = static_cast<std::size_t>(1099511628211ull);
		}
		else {
			result = static_cast<std::size_t>(2166136261u);
			prime = static_cast<std::size_t>(16777619u);
		}

		const auto hash_text = [&result, prime](std::string_view text) noexcept {
			for (const auto character : text) {
				result ^= static_cast<std::size_t>(static_cast<unsigned char>(character));
				result *= prime;
			}
		};

		hash_text(value.GetNamespace());

		result ^= static_cast<std::size_t>(static_cast<unsigned char>(':'));
		result *= prime;

		hash_text(value.GetPath());
		return result;
	}
};
