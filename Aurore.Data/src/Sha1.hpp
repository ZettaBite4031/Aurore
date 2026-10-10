#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Aurore::Data::Detail {
	class Sha1 final {
	public:
		void Update(std::span<const std::byte> data) noexcept;

		[[nodiscard]] std::array<std::byte, 20> Finalize() const noexcept;

	private:
		void ProcessBlock(const std::byte* block) noexcept;

		std::array<std::uint32_t, 5> m_State{
			0x67452301u,
			0xEFCDAB89u,
			0x98BADCFEu,
			0x10325476u,
			0xC3D2E1F0u,
		};

		std::array<std::byte, 64> m_Buffer{};
		std::size_t m_BufferSize{ 0 };
		std::uint64_t m_TotalBytes{ 0 };
	};

	[[nodiscard]] std::string ToHex(std::span<const std::byte> bytes);
}
