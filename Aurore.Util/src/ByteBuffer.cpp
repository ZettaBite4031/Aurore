#include <Aurore/Util/ByteBuffer.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
	template<typename T>
	concept UnsignedInteger =
		std::is_integral_v<T> &&
		std::is_unsigned_v<T>;

	template<UnsignedInteger T>
	void AppendBigEndian(std::vector<std::byte>& destination, T value) {
		for (std::size_t index = sizeof(T); index > 0; --index) {
			const auto shift = (index - 1) * 8;
			const auto current = static_cast<std::uint8_t>((value >> shift) & static_cast<T>(0xFF));

			destination.push_back(static_cast<std::byte>(current));
		}
	}

	template<UnsignedInteger T>
	bool TryReadBigEndian(
		std::span<const std::byte> data,
		std::size_t position,
		T& result) noexcept {

		if (position > data.size() || sizeof(T) > data.size() - position) {
			return false;
		}

		T decoded = 0;

		for (std::size_t index = 0; index < sizeof(T); ++index) {
			const auto current = std::to_integer<std::uint8_t>(data[position + index]);

			decoded = static_cast<T>(
				(decoded << 8) |
				static_cast<T>(current)
				);
		}

		result = decoded;
		return true;
	}
}

namespace Aurore::Util {
	ByteBuffer::ByteBuffer(std::size_t initial_capacity) {
		m_Data.reserve(initial_capacity);
	}

	void ByteBuffer::Reserve(std::size_t capacity) {
		m_Data.reserve(capacity);
	}

	void ByteBuffer::Clear() noexcept {
		m_Data.clear();
	}

	bool ByteBuffer::Empty() const noexcept {
		return m_Data.empty();
	}

	std::size_t ByteBuffer::Size() const noexcept {
		return m_Data.size();
	}

	const std::byte* ByteBuffer::Data() const noexcept {
		return m_Data.data();
	}

	std::span<const std::byte> ByteBuffer::Bytes() const noexcept {
		return m_Data;
	}

	void ByteBuffer::WriteByte(std::byte value) {
		m_Data.push_back(value);
	}

	void ByteBuffer::WriteUnsignedByte(std::uint8_t value) {
		WriteByte(static_cast<std::byte>(value));
	}

	void ByteBuffer::WriteBool(bool value) {
		WriteUnsignedByte(value ? 1u : 0u);
	}

	void ByteBuffer::WriteUInt16(std::uint16_t value) {
		AppendBigEndian(m_Data, value);
	}

	void ByteBuffer::WriteInt16(std::int16_t value) {
		const auto bits = std::bit_cast<std::uint16_t>(value);
		AppendBigEndian(m_Data, bits);
	}

	void ByteBuffer::WriteInt32(std::int32_t value) {
		const auto bits = std::bit_cast<std::uint32_t>(value);
		AppendBigEndian(m_Data, bits);
	}

	void ByteBuffer::WriteInt64(std::int64_t value) {
		const auto bits = std::bit_cast<std::uint64_t>(value);
		AppendBigEndian(m_Data, bits);
	}

	void ByteBuffer::WriteFloat(float value) {
		const auto bits = std::bit_cast<std::uint32_t>(value);
		AppendBigEndian(m_Data, bits);
	}

	void ByteBuffer::WriteDouble(double value) {
		const auto bits = std::bit_cast<std::uint64_t>(value);
		AppendBigEndian(m_Data, bits);
	}

	void ByteBuffer::WriteVarInt(std::int32_t value) {
		auto encoded = std::bit_cast<std::uint32_t>(value);

		do {
			auto current_byte = static_cast<std::uint8_t>(encoded & 0x7Fu);
			encoded >>= 7;
			if (encoded != 0) current_byte |= 0x80u;
			WriteUnsignedByte(current_byte);
		} while (encoded != 0);
	}

	void ByteBuffer::WriteVarLong(std::int64_t value) {
		auto encoded = std::bit_cast<std::uint64_t>(value);

		do {
			auto current_byte = static_cast<std::uint8_t>(encoded & 0x7Fu);
			encoded >>= 7;
			if (encoded != 0) current_byte |= 0x80u;
			WriteUnsignedByte(current_byte);
		} while (encoded != 0);
	}

	void ByteBuffer::WriteBytes(std::span<const std::byte> bytes) {
		m_Data.insert(m_Data.end(), bytes.begin(), bytes.end());
	}

	void ByteBuffer::WriteString(std::string_view value) {
		if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
			throw std::length_error("String is too large for VarInt!");

		WriteVarInt(static_cast<std::int32_t>(value.size()));

		const auto characters = std::span<const char>(value.data(), value.size());
		WriteBytes(std::as_bytes(characters));
	}

	void ByteBuffer::WriteUuid(const Uuid& value) {
		WriteBytes(value.Bytes());
	}

	ByteReader::ByteReader(std::span<const std::byte> data) noexcept
		: m_Data(data.data(), data.size()) {}

	std::size_t ByteReader::Position() const noexcept {
		return m_Position;
	}

	std::size_t ByteReader::Remaining() const noexcept {
		if (m_Position >= m_Data.size()) return 0;

		return m_Data.size() - m_Position;
	}

	bool ByteReader::Empty() const noexcept {
		return Remaining() == 0;
	}

	void ByteReader::Reset() noexcept {
		m_Position = 0;
	}

	BufferResult<std::byte> ByteReader::ReadByte() noexcept {
		if (m_Position >= m_Data.size()) {
			return std::unexpected(BufferError::IncompleteData);
		}
		return m_Data[m_Position++];
	}

	BufferResult<std::uint8_t> ByteReader::ReadUnsignedByte() noexcept {
		const auto value = ReadByte();
		if (!value) {
			return std::unexpected(value.error());
		}
		return std::to_integer<std::uint8_t>(*value);
	}

	BufferResult<bool> ByteReader::ReadBool() noexcept {
		const auto value = ReadUnsignedByte();
		if (!value) {
			return std::unexpected(value.error());
		}
		return *value != 0;
	}

	BufferResult<std::uint16_t> ByteReader::ReadUInt16() noexcept {
		std::uint16_t value{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, value)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(value);
		return value;
	}

	BufferResult<std::int16_t> ByteReader::ReadInt16() noexcept {
		std::uint16_t bits{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, bits)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(bits);
		return std::bit_cast<std::int16_t>(bits);
	}

	BufferResult<std::int32_t> ByteReader::ReadInt32() noexcept {
		std::uint32_t bits{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, bits)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(bits);
		return std::bit_cast<std::int32_t>(bits);
	}

	BufferResult<std::int64_t> ByteReader::ReadInt64() noexcept {
		std::uint64_t bits{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, bits)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(bits);
		return std::bit_cast<std::int64_t>(bits);
	}

	BufferResult<float> ByteReader::ReadFloat() noexcept {
		std::uint32_t bits{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, bits)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(bits);
		return std::bit_cast<float>(bits);
	}

	BufferResult<double> ByteReader::ReadDouble() noexcept {
		std::uint64_t bits{ 0 };
		if (!TryReadBigEndian(m_Data, m_Position, bits)) {
			return std::unexpected(BufferError::IncompleteData);
		}
		m_Position += sizeof(bits);
		return std::bit_cast<double>(bits);
	}

	BufferResult<std::int32_t> ByteReader::ReadVarInt() noexcept {
		std::uint32_t result{ 0 };
		auto cursor{ m_Position };

		for (std::uint32_t byte_index{ 0 }; byte_index < 5; byte_index++) {
			if (cursor >= m_Data.size())
				return std::unexpected(BufferError::IncompleteData);

			const auto current_byte = std::to_integer<std::uint8_t>(m_Data[cursor++]);
			const auto payload = static_cast<std::uint32_t>(current_byte & 0x7Fu);
			/*
				A 32-bit VarInt has only four usable payload bits in its
				fifth byte. Higher bits would exceed the representable
				32-bit range.
			*/
			if (byte_index == 4 && payload > 0xFu)
				return std::unexpected(BufferError::VarIntTooLong);

			result |= payload << (byte_index * 7);
			if ((current_byte & 0x80u) == 0) {
				m_Position = cursor;
				return std::bit_cast<std::int32_t>(result);
			}
		}
		return std::unexpected(BufferError::VarIntTooLong);
	}

	BufferResult<std::int64_t> ByteReader::ReadVarLong() noexcept {
		std::uint64_t result = 0;
		std::size_t cursor = m_Position;

		for (std::uint32_t byteIndex = 0; byteIndex < 10; ++byteIndex) {
			if (cursor >= m_Data.size()) {
				return std::unexpected(BufferError::IncompleteData);
			}

			const auto currentByte =
				std::to_integer<std::uint8_t>(m_Data[cursor++]);

			const auto payload =
				static_cast<std::uint64_t>(currentByte & 0x7Fu);

			/*
				A 64-bit VarLong has only one usable payload bit in its
				tenth byte. Any higher payload bits would exceed 64 bits.
			*/
			if (byteIndex == 9 && payload > 0x01u) {
				return std::unexpected(BufferError::VarLongTooLong);
			}

			result |= payload << (byteIndex * 7);

			if ((currentByte & 0x80u) == 0) {
				m_Position = cursor;
				return std::bit_cast<std::int64_t>(result);
			}
		}

		return std::unexpected(BufferError::VarLongTooLong);
	}

	BufferResult<std::span<const std::byte>> ByteReader::ReadBytes(std::size_t count) noexcept {
		if (m_Position > m_Data.size() || count > m_Data.size() - m_Position) {
			return std::unexpected(BufferError::IncompleteData);
		}
		const auto result = m_Data.subspan(m_Position, count);
		m_Position += count;
		return result;
	}

	BufferResult<std::string> ByteReader::ReadString(std::size_t maximum_encoded_bytes) {
		/*
			Read through a temporary reader so a malformed or incomplete
			string does not partially advance the real reader.
		*/
		auto temp = *this;

		const auto length = temp.ReadVarInt();

		if (!length) {
			return std::unexpected(length.error());
		}

		if (*length < 0) {
			return std::unexpected(BufferError::NegativeLength);
		}

		const auto unsigned_length = static_cast<std::size_t>(*length);
		if (unsigned_length > maximum_encoded_bytes) {
			return std::unexpected(BufferError::LengthLimitExceeded);
		}

		const auto bytes = temp.ReadBytes(unsigned_length);
		if (!bytes) {
			return std::unexpected(bytes.error());
		}

		const auto* characters = reinterpret_cast<const char*>(bytes->data());

		std::string result{
			characters,
			bytes->size()
		};

		*this = temp;

		return result;
	}

	BufferResult<Uuid> ByteReader::ReadUuid() noexcept {
		const auto bytes = ReadBytes(Uuid::ByteCount);
		if (!bytes) return std::unexpected(bytes.error());
		Uuid::Storage storage{};
		std::copy(bytes->begin(), bytes->end(), storage.begin());
		return Uuid{ storage };
	}
}

