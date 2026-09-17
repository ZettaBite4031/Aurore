#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Aurore::Util {
	enum class UuidError : std::uint8_t {
		InvalidLength,
		InvalidFormat,
		InvalidCharacter,
	};

	class Uuid final {
	public:
		static constexpr std::size_t ByteCount{ 16 };

		using Storage = std::array<std::byte, ByteCount>;

		constexpr Uuid() noexcept = default;

		explicit constexpr Uuid(Storage bytes) noexcept
			: m_Bytes(bytes) {}

		[[nodiscard]] static std::expected<Uuid, UuidError> Parse(std::string_view value) noexcept;

		[[nodiscard]] static Uuid FromOfflinePlayerName(std::string_view username);

		[[nodiscard]] std::string ToString() const;

		[[nodiscard]] constexpr std::span<const std::byte, ByteCount> Bytes() const noexcept {
			return std::span<const std::byte, ByteCount>{ m_Bytes };
		}

		[[nodiscard]] constexpr bool IsNil() const noexcept {
			for (const auto byte : m_Bytes)
				if (byte != std::byte{ 0 }) return false;
			return true;
		}

		auto operator<=>(const Uuid&) const noexcept = default;

	private:
		Storage m_Bytes{};
	};
}

template<>
struct std::hash<Aurore::Util::Uuid> {
	std::size_t operator()(const Aurore::Util::Uuid& value) const noexcept {
		std::size_t result;

		if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t))
			result = static_cast<std::size_t>(14695981039346656037ull);
		else result = static_cast<std::size_t>(2166136261u);

		const std::size_t prime = sizeof(std::size_t) >= sizeof(std::uint64_t)
			? static_cast<std::size_t>(1099511628211ull)
			: static_cast<std::size_t>(16777619u);

		for (const auto byte : value.Bytes()) {
			result ^= static_cast<std::size_t>(std::to_integer<std::uint8_t>(byte));
			result *= prime;
		}
		return result;
	}
};


