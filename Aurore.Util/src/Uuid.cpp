#include <Aurore/Util/UUID.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace{
	constexpr std::array<std::uint32_t, 64> Md5ShiftAmounts{
		7, 12, 17, 22,
		7, 12, 17, 22,
		7, 12, 17, 22,
		7, 12, 17, 22,

		5, 9, 14, 20,
		5, 9, 14, 20,
		5, 9, 14, 20,
		5, 9, 14, 20,

		4, 11, 16, 23,
		4, 11, 16, 23,
		4, 11, 16, 23,
		4, 11, 16, 23,

		6, 10, 15, 21,
		6, 10, 15, 21,
		6, 10, 15, 21,
		6, 10, 15, 21,
	};

	constexpr std::array<std::uint32_t, 64> Md5Constants{
		0xd76aa478, 0xe8c7b756,
		0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a,
		0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af,
		0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193,
		0xa679438e, 0x49b40821,

		0xf61e2562, 0xc040b340,
		0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453,
		0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6,
		0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8,
		0x676f02d9, 0x8d2a4c8a,

		0xfffa3942, 0x8771f681,
		0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9,
		0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa,
		0xd4ef3085, 0x04881d05,
		0xd9d4d039, 0xe6db99e5,
		0x1fa27cf8, 0xc4ac5665,

		0xf4292244, 0x432aff97,
		0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92,
		0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0,
		0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235,
		0x2ad7d2bb, 0xeb86d391,
	};

	std::uint32_t ReadLittleEndian32(std::span<const std::byte> data, std::size_t offset) noexcept {
		return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset]))
			| (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8)
			| (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16)
			| (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
	}

	void WriteLittleEndian32(Aurore::Util::Uuid::Storage& destination, std::size_t offset, std::uint32_t value) noexcept {
		for (std::size_t index{ 0 }; index < 4; ++index)
			destination[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFFu);
	}

	Aurore::Util::Uuid::Storage ComputeMd5(std::span<const std::byte> input) {
		if (input.size() > std::numeric_limits<std::uint64_t>::max() / 8)
			throw std::length_error("Input is too large for MD5!");

		std::vector<std::byte> msg(input.begin(), input.end());
		const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8;
		msg.push_back(std::byte{ 0x80 });
		while ((msg.size() % 64) != 56)
			msg.push_back(std::byte{ 0x00 });

		for (std::size_t index{ 0 }; index < 8; index++)
			msg.push_back(static_cast<std::byte>((bit_length >> (index * 8)) & 0xFFu));

		std::uint32_t a0{ 0x67452301 };
		std::uint32_t b0{ 0xefcdab89 };
		std::uint32_t c0{ 0x98badcfe };
		std::uint32_t d0{ 0x10325476 };

		for (std::size_t offset{ 0 }; offset < msg.size(); offset += 64) {
			std::array<std::uint32_t, 16> words{};
			for (std::size_t index{ 0 }; index < words.size(); index++)
				words[index] = ReadLittleEndian32(msg, offset + index * 4);

			auto a = a0;
			auto b = b0;
			auto c = c0;
			auto d = d0;

			for (std::uint32_t index{ 0 }; index < 64; index++) {
				std::uint32_t function;
				std::uint32_t word_index;

				if (index < 16) {
					function = (b & c) | (~b & d);
					word_index = index;
				}
				else if (index < 32) {
					function = (d & b) | (~d & c);
					word_index = (5 * index + 1) % 16;
				}
				else if (index < 48) {
					function = b ^ c ^ d;
					word_index = (3 * index + 5) % 16;
				}
				else {
					function = c ^ (b | ~d);
					word_index = (7 * index) % 16;
				}

				const auto previous_d = d;
				d = c;
				c = b;

				b += std::rotl(a + function + Md5Constants[index] + words[word_index], static_cast<int>(Md5ShiftAmounts[index]));
				a = previous_d;
			}

			a0 += a;
			b0 += b;
			c0 += c;
			d0 += d;
		}

		Aurore::Util::Uuid::Storage digest{};
		WriteLittleEndian32(digest, 0, a0);
		WriteLittleEndian32(digest, 4, b0);
		WriteLittleEndian32(digest, 8, c0);
		WriteLittleEndian32(digest, 12, d0);
		return digest;
	}

	std::uint8_t DecodeHex(char character) noexcept {
		if (character >= '0' && character <= '9')
			return static_cast<std::uint8_t>(character - '0');
		if (character >= 'a' && character <= 'f')
			return static_cast<std::uint8_t>(character - 'a' + 10);
		if (character >= 'A' && character <= 'F')
			return static_cast<std::uint8_t>(character - 'A' + 10);
		return 0xFFu;
	}
}

namespace Aurore::Util {
	std::expected<Uuid, UuidError> Uuid::Parse(std::string_view value) noexcept {
		if (value.size() != 36)
			return std::unexpected(UuidError::InvalidLength);

		constexpr std::array<std::size_t, 4> Separators{ 8, 13, 18, 23 };
		for (const auto separator : Separators)
			if (value[separator] != '-')
				return std::unexpected(UuidError::InvalidFormat);

		Storage bytes{};

		std::size_t output_index{ 0 };
		for (std::size_t input_index{ 0 }; input_index < value.size();) {
			if (value[input_index] == '-') {
				input_index++;
				continue;
			}

			if (input_index + 1 >= value.size() || output_index >= bytes.size())
				return std::unexpected(UuidError::InvalidFormat);

			const auto high = DecodeHex(value[input_index]);
			const auto low = DecodeHex(value[input_index + 1]);
			if (high == 0xFFu || low == 0xFFu)
				return std::unexpected(UuidError::InvalidCharacter);

			bytes[output_index++] = static_cast<std::byte>((high << 4) | low);
			input_index += 2;
		}

		if (output_index != bytes.size())
			return std::unexpected(UuidError::InvalidFormat);

		return Uuid{ bytes };
	}

	Uuid Uuid::FromOfflinePlayerName(std::string_view username) {
		constexpr std::string_view Prefix{ "OfflinePlayer:" };
		std::vector<std::byte> input;

		input.reserve(Prefix.size() + username.size());
		const auto append_text = [&input](std::string_view value) {
			const auto characters = std::span<const char>{ value.data(), value.size() };
			const auto bytes = std::as_bytes(characters);
			input.insert(input.end(), bytes.begin(), bytes.end());
		};

		append_text(Prefix);
		append_text(username);

		auto bytes = ComputeMd5(input);

		bytes[6] = static_cast<std::byte>((std::to_integer<std::uint8_t>(bytes[6]) & 0xFu) | 0x30u);
		bytes[8] = static_cast<std::byte>((std::to_integer<std::uint8_t>(bytes[8]) & 0x3Fu) | 0x80u);
		return Uuid{ bytes };
	}

	std::string Uuid::ToString() const {
		static constexpr char HexDigits[]{ "0123456789ABCDEF" };

		std::string result;
		result.reserve(36);
		for (std::size_t index{ 0 }; index < m_Bytes.size(); index++) {
			if (index == 4 || index == 6 || index == 8 || index == 10)
				result.push_back('-');

			const auto byte = std::to_integer<std::uint8_t>(m_Bytes[index]);
			result.push_back(HexDigits[(byte >> 4) & 0xFu]);
			result.push_back(HexDigits[byte & 0xFu]);
		}
		return result;
	}
}
