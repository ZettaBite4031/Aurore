#include "Sha1.hpp"

#include <algorithm>
#include <bit>

namespace Aurore::Data::Detail {
	namespace {
		[[nodiscard]] std::uint32_t LoadBigEndian(const std::byte* data) noexcept {
			return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[0])) << 24)
				| (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[1])) << 16)
				| (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[2])) << 8)
				| static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[3]));
		}

		void StoreBigEndian(std::uint32_t value, std::byte* output) noexcept {
			output[0] = static_cast<std::byte>((value >> 24) & 0xFFu);
			output[1] = static_cast<std::byte>((value >> 16) & 0xFFu);
			output[2] = static_cast<std::byte>((value >> 8) & 0xFFu);
			output[3] = static_cast<std::byte>(value & 0xFFu);
		}
	}

	void Sha1::Update(std::span<const std::byte> data) noexcept {
		m_TotalBytes += static_cast<std::uint64_t>(data.size());
		while (!data.empty()) {
			const std::size_t available = m_Buffer.size() - m_BufferSize;
			const std::size_t copy_size = std::min(available, data.size());
			std::copy_n(data.begin(), copy_size, m_Buffer.begin() + m_BufferSize);
			m_BufferSize += copy_size;
			data = data.subspan(copy_size);
			if (m_BufferSize != m_Buffer.size()) continue;
			ProcessBlock(m_Buffer.data());
			m_BufferSize = 0;
		}
	}

	std::array<std::byte, 20> Sha1::Finalize() const noexcept {
		Sha1 working{ *this };
		const std::uint64_t bit_length = working.m_TotalBytes * 8u;
		working.m_Buffer[working.m_BufferSize++] = std::byte{ 0x80 };
		if (working.m_BufferSize > 56) {
			std::fill(working.m_Buffer.begin() + working.m_BufferSize, working.m_Buffer.end(), std::byte{ 0 });
			working.ProcessBlock(working.m_Buffer.data());
			working.m_BufferSize = 0;
		}

		std::fill(working.m_Buffer.begin() + working.m_BufferSize, working.m_Buffer.begin() + 56, std::byte{ 0 });
		for (std::size_t i = 0; i < 8; i++) {
			const auto shift = static_cast<unsigned int>(56 - (i * 8));
			working.m_Buffer[56 + i] = static_cast<std::byte>((bit_length >> shift) & 0xFFu);
		}

		working.ProcessBlock(working.m_Buffer.data());
		std::array<std::byte, 20> digest{};
		for (std::size_t i = 0; i < working.m_State.size(); i++) {
			StoreBigEndian(working.m_State[i], digest.data() + (i * 4));
		}

		return digest;
	}

	void Sha1::ProcessBlock(const std::byte* block) noexcept {
		std::array<std::uint32_t, 80> words{};
		for (std::size_t i = 0; i < 16; i++) 
			words[i] = LoadBigEndian(block + (i * 4));
		for (std::size_t i = 16; i < words.size(); i++)
			words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

		std::uint32_t a = m_State[0];
		std::uint32_t b = m_State[1];
		std::uint32_t c = m_State[2];
		std::uint32_t d = m_State[3];
		std::uint32_t e = m_State[4];

		for (std::size_t i{ 0 }; i < words.size(); i++) {
			std::uint32_t function{ 0 };
			std::uint32_t constant{ 0 };
			if (i < 20) {
				function = (b & c) | ((~b) & d);
				constant = 0x5A827999u;
			}
			else if (i < 40) {
				function = b ^ c ^ d;
				constant = 0x6ED9EBA1u;
			}
			else if (i < 60) {
				function = (b & c) | (b & d) | (c & d);
				constant = 0x8F1BBCDCu;
			}
			else {
				function = b ^ c ^ d;
				constant = 0xCA62C1D6u;
			}

			const std::uint32_t temp = std::rotl(a, 5) + function + e + constant + words[i];

			e = d;
			d = c;
			c = std::rotl(b, 30);
			b = a;
			a = temp;
		}

		m_State[0] += a;
		m_State[1] += b;
		m_State[2] += c;
		m_State[3] += d;
		m_State[4] += e;
	}

	std::string ToHex(std::span<const std::byte> bytes) {
		constexpr char Digits[]{ "0123456789abcdef" };

		std::string result;
		result.resize(bytes.size() * 2);
		for (std::size_t index{ 0 }; index < bytes.size(); ++index) {
			const auto value = std::to_integer<std::uint8_t>(bytes[index]);
			result[index * 2] = Digits[value >> 4];
			result[index * 2 + 1] = Digits[value & 0x0F];
		}

		return result;
	}
}
