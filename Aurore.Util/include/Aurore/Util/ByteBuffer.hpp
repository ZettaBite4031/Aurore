#pragma once

#include "UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Aurore::Util {
	enum class BufferError : std::uint8_t {
		IncompleteData,
		VarIntTooLong,
		VarLongTooLong,
		NegativeLength,
		LengthLimitExceeded,
	};

	template<typename T>
	using BufferResult = std::expected<T, BufferError>;

	class ByteBuffer final {
	public:
		ByteBuffer() = default;
		explicit ByteBuffer(std::size_t initial_capacity);

		void Reserve(std::size_t capacity);
		void Clear() noexcept;

		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] const std::byte* Data() const noexcept;
		[[nodiscard]] std::span<const std::byte> Bytes() const noexcept;

		void WriteByte(std::byte value);
		void WriteUnsignedByte(std::uint8_t value);
		void WriteBool(bool value);
		void WriteUInt16(std::uint16_t value);
		void WriteInt16(std::int16_t value);
		void WriteInt32(std::int32_t value);
		void WriteInt64(std::int64_t value);
		void WriteFloat(float value);
		void WriteDouble(double value);
		void WriteVarInt(std::int32_t value);
		void WriteVarLong(std::int64_t value);
		void WriteBytes(std::span<const std::byte> bytes);
		void WriteString(std::string_view value);
		void WriteUuid(const Uuid& value);

	private:
		std::vector<std::byte> m_Data;
	};

	class ByteReader final {
	public:
		explicit ByteReader(std::span<const std::byte> data) noexcept;

		[[nodiscard]] std::size_t Position() const noexcept;
		[[nodiscard]] std::size_t Remaining() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;

		void Reset() noexcept;

		[[nodiscard]] BufferResult<std::byte> ReadByte() noexcept;
		[[nodiscard]] BufferResult<std::uint8_t> ReadUnsignedByte() noexcept;
		[[nodiscard]] BufferResult<bool> ReadBool() noexcept;
		[[nodiscard]] BufferResult<std::uint16_t> ReadUInt16() noexcept;
		[[nodiscard]] BufferResult<std::int16_t> ReadInt16() noexcept;
		[[nodiscard]] BufferResult<std::int32_t> ReadInt32() noexcept;
		[[nodiscard]] BufferResult<std::int64_t> ReadInt64() noexcept;
		[[nodiscard]] BufferResult<float> ReadFloat() noexcept;
		[[nodiscard]] BufferResult<double> ReadDouble() noexcept;
		[[nodiscard]] BufferResult<std::int32_t> ReadVarInt() noexcept;
		[[nodiscard]] BufferResult<std::int64_t> ReadVarLong() noexcept;
		[[nodiscard]] BufferResult<std::span<const std::byte>> ReadBytes(std::size_t count) noexcept;
		[[nodiscard]] BufferResult<std::string> ReadString(std::size_t maximum_encoded_bytes);
		[[nodiscard]] BufferResult<Uuid> ReadUuid() noexcept;

	private:
		std::span<const std::byte> m_Data;
		std::size_t m_Position{ 0 };
	};
}
