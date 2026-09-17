#include <Aurore/Util/NbtBinary.hpp>

#include <bit>
#include <limits>
#include <utility>

namespace {
	using namespace Aurore::Util;

	constexpr std::size_t MaximumEncodedStringBytes{ 65'535 };

	NbtReadError MakeReadError(NbtReadErrorCode code, std::size_t offset, std::size_t depth) noexcept {
		return NbtReadError{ .Code = code, .Offset = offset, .Depth = depth };
	}

	NbtWriteError MakeWriteError(NbtWriteErrorCode code, std::size_t depth) noexcept {
		return NbtWriteError{ .Code = code, .Depth = depth };
	}

	class NbtDecoder final {
	public:
		NbtDecoder(std::span<const std::byte> data, NbtLimits limits) noexcept
			: m_Data(data), m_Limits(limits) {}

		[[nodiscard]] std::size_t Position() const noexcept { return m_Position; }

		[[nodiscard]] std::expected<NbtDocument, NbtReadError> ReadDocument() {
			const auto type = ReadType(0);
			if (!type) return std::unexpected(type.error());
			if (*type == NbtType::End)
				return std::unexpected(MakeReadError(NbtReadErrorCode::UnexpectedEndTag, 0, 0));

			const auto name = ReadString(0);
			if (!name) return std::unexpected(name.error());

			const auto root = ReadPayload(*type, 0);
			if (!root) return std::unexpected(root.error());

			return NbtDocument{ .Name = std::move(name).value(), .Root = std::move(root).value() };
		}

		[[nodiscard]] std::expected<NbtCompound, NbtReadError> ReadNetworkCompound() {
			const auto type_offset = m_Position;
			const auto type = ReadType(0);
			if (!type) return std::unexpected(type.error());
			if (*type != NbtType::Compound)
				return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidRootType, type_offset, 0));

			return ReadCompoundPayload(0);
		}

	private:

		[[nodiscard]] std::expected<void, NbtReadError> CheckReadable(std::size_t count, std::size_t depth) const noexcept {
			if (m_Position > m_Limits.MaximumTotalBytes || count > m_Limits.MaximumTotalBytes - m_Position)
				return std::unexpected(MakeReadError(NbtReadErrorCode::TotalByteLimitExceeded, m_Position, depth));
			if (m_Position > m_Data.size() || count > m_Data.size() - m_Position)
				return std::unexpected(MakeReadError(NbtReadErrorCode::IncompleteData, m_Position, depth));
			return {};
		}

		[[nodiscard]] std::expected<std::span<const std::byte>, NbtReadError> ReadBytes(std::size_t count, std::size_t depth) noexcept {
			const auto readable = CheckReadable(count, depth);
			if (!readable) return std::unexpected(readable.error());

			const auto result = m_Data.subspan(m_Position, count);
			m_Position += count;
			return result;
		}

		[[nodiscard]] std::expected<std::uint8_t, NbtReadError> ReadUnsignedByte(std::size_t depth) noexcept {
			const auto bytes = ReadBytes(1, depth);
			if (!bytes) return std::unexpected(bytes.error());
			return std::to_integer<std::uint8_t>((*bytes)[0]);
		}

		[[nodiscard]] std::expected<std::int8_t, NbtReadError> ReadByte(std::size_t depth) noexcept {
			const auto value = ReadUnsignedByte(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<std::int8_t>(*value);
		}

		[[nodiscard]] std::expected<std::uint16_t, NbtReadError> ReadUnsignedShort(std::size_t depth) noexcept {
			const auto bytes = ReadBytes(2, depth);
			if (!bytes) return std::unexpected(bytes.error());

			return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[0])) << 8
				| static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[1]));
		}

		[[nodiscard]] std::expected<std::int16_t, NbtReadError> ReadShort(std::size_t depth) noexcept {
			const auto value = ReadUnsignedShort(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<std::int16_t>(*value);
		}

		[[nodiscard]] std::expected<std::uint32_t, NbtReadError> ReadUnsignedInt(std::size_t depth) noexcept {
			const auto bytes = ReadBytes(4, depth);
			if (!bytes) return std::unexpected(bytes.error());

			return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*bytes)[0])) << 24
				| static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*bytes)[1])) << 16
				| static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*bytes)[2])) << 8
				| static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*bytes)[3]));
		}

		[[nodiscard]] std::expected<std::int32_t, NbtReadError> ReadInt(std::size_t depth) noexcept {
			const auto value = ReadUnsignedInt(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<std::int32_t>(*value);
		}

		[[nodiscard]] std::expected<std::uint64_t, NbtReadError> ReadUnsignedLong(std::size_t depth) noexcept {
			const auto bytes = ReadBytes(8, depth);
			if (!bytes) return std::unexpected(bytes.error());

			std::uint64_t value{ 0 };
			for (const auto byte : *bytes)
				value = (value << 8) | std::to_integer<std::uint8_t>(byte);
			return value;
		}

		[[nodiscard]] std::expected<std::int64_t, NbtReadError> ReadLong(std::size_t depth) noexcept {
			const auto value = ReadUnsignedLong(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<std::int64_t>(*value);
		}

		[[nodiscard]] std::expected<float, NbtReadError> ReadFloat(std::size_t depth) noexcept {
			const auto value = ReadUnsignedInt(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<float>(*value);
		}

		[[nodiscard]] std::expected<double, NbtReadError> ReadDouble(std::size_t depth) noexcept {
			const auto value = ReadUnsignedLong(depth);
			if (!value) return std::unexpected(value.error());
			return std::bit_cast<double>(*value);
		}

		[[nodiscard]] std::expected<NbtType, NbtReadError> ReadType(std::size_t depth) noexcept {
			const auto offset = m_Position;
			const auto value = ReadUnsignedByte(depth);
			if (!value) return std::unexpected(value.error());
			if (*value > static_cast<std::uint8_t>(NbtType::LongArray))
				return std::unexpected(MakeReadError(NbtReadErrorCode::UnknownTagType, offset, depth));
			return static_cast<NbtType>(*value);
		}

		[[nodiscard]] std::expected<NbtString, NbtReadError> ReadString(std::size_t depth) {
			const auto length_offset = m_Position;
			const auto encoded_length = ReadUnsignedShort(depth);
			if (!encoded_length) return std::unexpected(encoded_length.error());

			const auto maximum_string_bytes = std::min(m_Limits.MaximumStringBytes, MaximumEncodedStringBytes);
			if (*encoded_length > maximum_string_bytes)
				return std::unexpected(MakeReadError(NbtReadErrorCode::StringByteLimitExceeded, length_offset, depth));

			const auto content_offset = m_Position;
			const auto bytes = ReadBytes(*encoded_length, depth);
			if (!bytes) return std::unexpected(bytes.error());

			std::u16string result;
			result.reserve(bytes->size());

			for (std::size_t index{ 0 }; index < bytes->size();) {
				const auto first = std::to_integer<std::uint8_t>((*bytes)[index]);
				if (first >= 0x01u && first <= 0x7Fu) {
					result.push_back(static_cast<char16_t>(first));
					++index;
					continue;
				}

				if (first >= 0xC0u && first <= 0xDFu) {
					if (index + 1 >= bytes->size())
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index, depth));

					const auto second = std::to_integer<std::uint8_t>((*bytes)[index + 1]);
					if ((second & 0xC0u) != 0x80u)
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index + 1, depth));

					const auto code_unit = static_cast<std::uint16_t>(((first & 0x1Fu) << 6) | (second & 0x3Fu));
					if (code_unit != 0 && code_unit < 0x80u)
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index, depth));

					result.push_back(static_cast<char16_t>(code_unit));
					index += 2;
					continue;
				}

				if (first >= 0xE0u && first <= 0xEFu) {
					if (index + 2 >= bytes->size())
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index, depth));

					const auto second = std::to_integer<std::uint8_t>((*bytes)[index + 1]);
					const auto third = std::to_integer<std::uint8_t>((*bytes)[index + 2]);
					if ((second & 0xC0u) != 0x80u)
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index + 1, depth));
					if ((third & 0xC0u) != 0x80u)
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index + 2, depth));

					const auto code_unit = static_cast<std::uint16_t>(((first & 0x0Fu) << 12)
						| ((second & 0x3Fu) << 6)
						| (third & 0x3Fu));
					if (code_unit < 0x800u)
						return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index, depth));

					result.push_back(static_cast<char16_t>(code_unit));
					index += 3;
					continue;
				}

				return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidModifiedUtf8, content_offset + index, depth));
			}

			return NbtString{ std::move(result) };
		}

		[[nodiscard]] std::expected<std::size_t, NbtReadError> ReadArrayLength(std::size_t depth) noexcept {
			const auto length_offset = m_Position;
			const auto length = ReadInt(depth);
			if (!length) return std::unexpected(length.error());
			if (*length < 0)
				return std::unexpected(MakeReadError(NbtReadErrorCode::NegativeLength, length_offset, depth));
			return static_cast<std::size_t>(*length);
		}

		[[nodiscard]] std::expected<NbtByteArray, NbtReadError> ReadByteArray(std::size_t depth) {
			const auto count = ReadArrayLength(depth);
			if (!count) return std::unexpected(count.error());
			if (*count > m_Limits.MaximumArrayElements)
				return std::unexpected(MakeReadError(NbtReadErrorCode::ArrayElementLimitExceeded, m_Position - 4, depth));

			const auto bytes = ReadBytes(*count, depth);
			if (!bytes) return std::unexpected(bytes.error());

			NbtByteArray result;
			result.reserve(*count);
			for (const auto byte : *bytes)
				result.push_back(std::bit_cast<std::int8_t>(std::to_integer<std::uint8_t>(byte)));
			return result;
		}

		[[nodiscard]] std::expected<NbtIntArray, NbtReadError> ReadIntArray(std::size_t depth) {
			const auto count = ReadArrayLength(depth);
			if (!count) return std::unexpected(count.error());
			if (*count > m_Limits.MaximumArrayElements)
				return std::unexpected(MakeReadError(NbtReadErrorCode::ArrayElementLimitExceeded, m_Position - 4, depth));
			if (*count > std::numeric_limits<std::size_t>::max() / sizeof(std::int32_t))
				return std::unexpected(MakeReadError(NbtReadErrorCode::AllocationSizeOverflow, m_Position, depth));

			const auto readable = CheckReadable(*count * sizeof(std::int32_t), depth);
			if (!readable) return std::unexpected(readable.error());

			NbtIntArray result;
			result.reserve(*count);
			for (std::size_t index{ 0 }; index < *count; ++index) {
				const auto value = ReadInt(depth);
				if (!value) return std::unexpected(value.error());
				result.push_back(*value);
			}
			return result;
		}

		[[nodiscard]] std::expected<NbtLongArray, NbtReadError> ReadLongArray(std::size_t depth) {
			const auto count = ReadArrayLength(depth);
			if (!count) return std::unexpected(count.error());
			if (*count > m_Limits.MaximumArrayElements)
				return std::unexpected(MakeReadError(NbtReadErrorCode::ArrayElementLimitExceeded, m_Position - 4, depth));
			if (*count > std::numeric_limits<std::size_t>::max() / sizeof(std::int64_t))
				return std::unexpected(MakeReadError(NbtReadErrorCode::AllocationSizeOverflow, m_Position, depth));

			const auto readable = CheckReadable(*count * sizeof(std::int64_t), depth);
			if (!readable) return std::unexpected(readable.error());

			NbtLongArray result;
			result.reserve(*count);
			for (std::size_t index{ 0 }; index < *count; ++index) {
				const auto value = ReadLong(depth);
				if (!value) return std::unexpected(value.error());
				result.push_back(*value);
			}
			return result;
		}

		[[nodiscard]] constexpr std::size_t MinimumPayloadBytes(NbtType type) const noexcept {
			switch (type) {
			case NbtType::Byte: return 1;
			case NbtType::Short: return 2;
			case NbtType::Int: return 4;
			case NbtType::Long: return 8;
			case NbtType::Float: return 4;
			case NbtType::Double: return 8;
			case NbtType::ByteArray: return 4;
			case NbtType::String: return 2;
			case NbtType::List: return 5;
			case NbtType::Compound: return 1;
			case NbtType::IntArray: return 4;
			case NbtType::LongArray: return 4;
			case NbtType::End: return 0;
			}
			return 0;
		}

		[[nodiscard]] std::expected<NbtList, NbtReadError> ReadListPayload(std::size_t depth) {
			const auto type_offset = m_Position;
			const auto element_type = ReadType(depth);
			if (!element_type) return std::unexpected(element_type.error());

			const auto length_offset = m_Position;
			const auto encoded_count = ReadInt(depth);
			if (!encoded_count) return std::unexpected(encoded_count.error());

			const auto count = *encoded_count <= 0 ? std::size_t{ 0 } : static_cast<std::size_t>(*encoded_count);
			if (count > m_Limits.MaximumListElements)
				return std::unexpected(MakeReadError(NbtReadErrorCode::ListElementLimitExceeded, length_offset, depth));
			if (count != 0 && *element_type == NbtType::End)
				return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidListElementType, type_offset, depth));

			const auto minimum_payload_bytes = MinimumPayloadBytes(*element_type);
			if (minimum_payload_bytes != 0) {
				if (count > std::numeric_limits<std::size_t>::max() / minimum_payload_bytes)
					return std::unexpected(MakeReadError(NbtReadErrorCode::AllocationSizeOverflow, length_offset, depth));
				const auto readable = CheckReadable(count * minimum_payload_bytes, depth);
				if (!readable) return std::unexpected(readable.error());
			}

			std::vector<NbtValue> values;
			values.reserve(count);
			for (std::size_t index{ 0 }; index < count; ++index) {
				const auto value = ReadPayload(*element_type, depth + 1);
				if (!value) return std::unexpected(value.error());
				values.push_back(std::move(*value));
			}

			const auto result = NbtList::Create(*element_type, std::move(values));
			if (!result)
				return std::unexpected(MakeReadError(NbtReadErrorCode::InvalidListElementType, type_offset, depth));
			return *result;
		}

		[[nodiscard]] std::expected<NbtCompound, NbtReadError> ReadCompoundPayload(std::size_t depth) {
			NbtCompound result;
			std::size_t entry_count{ 0 };

			while (true) {
				const auto type = ReadType(depth);
				if (!type) return std::unexpected(type.error());
				if (*type == NbtType::End) return result;

				if (entry_count >= m_Limits.MaximumCompoundEntries)
					return std::unexpected(MakeReadError(NbtReadErrorCode::CompoundEntryLimitExceeded, m_Position - 1, depth));
				++entry_count;

				const auto name = ReadString(depth);
				if (!name) return std::unexpected(name.error());

				const auto value = ReadPayload(*type, depth + 1);
				if (!value) return std::unexpected(value.error());
				result.Set(std::move(name).value(), std::move(value).value());
			}
		}

		[[nodiscard]] std::expected<NbtValue, NbtReadError> ReadPayload(NbtType type, std::size_t depth) {
			if (depth > m_Limits.MaximumDepth)
				return std::unexpected(MakeReadError(NbtReadErrorCode::DepthLimitExceeded, m_Position, depth));

			switch (type) {
			case NbtType::Byte: {
				const auto value = ReadByte(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Byte(*value);
			}
			case NbtType::Short: {
				const auto value = ReadShort(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Short(*value);
			}
			case NbtType::Int: {
				const auto value = ReadInt(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Int(*value);
			}
			case NbtType::Long: {
				const auto value = ReadLong(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Long(*value);
			}
			case NbtType::Float: {
				const auto value = ReadFloat(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Float(*value);
			}
			case NbtType::Double: {
				const auto value = ReadDouble(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Double(*value);
			}
			case NbtType::ByteArray: {
				const auto value = ReadByteArray(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::ByteArray(std::move(value).value());
			}
			case NbtType::String: {
				const auto value = ReadString(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::String(std::move(value).value());
			}
			case NbtType::List: {
				const auto value = ReadListPayload(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::List(std::move(value).value());
			}
			case NbtType::Compound: {
				const auto value = ReadCompoundPayload(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::Compound(std::move(value).value());
			}
			case NbtType::IntArray: {
				const auto value = ReadIntArray(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::IntArray(std::move(value).value());
			}
			case NbtType::LongArray: {
				const auto value = ReadLongArray(depth);
				if (!value) return std::unexpected(value.error());
				return NbtValue::LongArray(std::move(value).value());
			}
			case NbtType::End:
				return std::unexpected(MakeReadError(NbtReadErrorCode::UnexpectedEndTag, m_Position, depth));
			}
			return std::unexpected(MakeReadError(NbtReadErrorCode::UnknownTagType, m_Position, depth));
		}

		std::span<const std::byte> m_Data;
		NbtLimits m_Limits;
		std::size_t m_Position{ 0 };
	};

	class NbtEncoder final {
	public:
		explicit NbtEncoder(NbtLimits limits) noexcept
			: m_Limits(limits) {}

		[[nodiscard]] std::expected<std::vector<std::byte>, NbtWriteError> WriteDocument(const NbtDocument& document) {
			if (document.Root.GetType() == NbtType::End)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidRootType, 0));

			const auto measured = MeasureDocument(document);
			if (!measured) return std::unexpected(measured.error());

			m_Data.reserve(*measured);
			WriteUnsignedByte(static_cast<std::uint8_t>(document.Root.GetType()));
			WriteString(document.Name);
			WritePayload(document.Root);
			return std::move(m_Data);
		}

		[[nodiscard]] std::expected<std::vector<std::byte>, NbtWriteError> WriteNetworkCompound(const NbtCompound& compound) {
			const auto measured_payload = MeasureCompound(compound, 0);
			if (!measured_payload) return std::unexpected(measured_payload.error());

			const auto measured = AddSizes(1, *measured_payload, 0);
			if (!measured) return std::unexpected(measured.error());

			m_Data.reserve(*measured);
			WriteUnsignedByte(static_cast<std::uint8_t>(NbtType::Compound));
			WriteCompound(compound);
			return std::move(m_Data);
		}

	private:
		[[nodiscard]] std::expected<std::size_t, NbtWriteError> AddSizes(
			std::size_t left,
			std::size_t right,
			std::size_t depth) const noexcept {

			if (right > std::numeric_limits<std::size_t>::max() - left)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::AllocationSizeOverflow, depth));

			const auto result = left + right;
			if (result > m_Limits.MaximumTotalBytes)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::TotalByteLimitExceeded, depth));
			return result;
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MultiplySizes(
			std::size_t count,
			std::size_t element_size,
			std::size_t depth) const noexcept {

			if (element_size != 0 && count > std::numeric_limits<std::size_t>::max() / element_size)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::AllocationSizeOverflow, depth));

			const auto result = count * element_size;
			if (result > m_Limits.MaximumTotalBytes)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::TotalByteLimitExceeded, depth));
			return result;
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasureModifiedUtf8(
			const NbtString& value,
			std::size_t depth) const noexcept {

			std::size_t result{ 0 };
			for (const auto code_unit : value.GetUtf16()) {
				const std::size_t encoded_size = code_unit >= 0x0001u && code_unit <= 0x007Fu ? 1
					: code_unit <= 0x07FFu ? 2
					: 3;

				if (encoded_size > std::numeric_limits<std::size_t>::max() - result)
					return std::unexpected(MakeWriteError(NbtWriteErrorCode::AllocationSizeOverflow, depth));
				result += encoded_size;
			}

			const auto maximum_string_bytes = std::min(m_Limits.MaximumStringBytes, MaximumEncodedStringBytes);
			if (result > maximum_string_bytes)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::StringByteLimitExceeded, depth));
			return result;
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasureString(
			const NbtString& value,
			std::size_t depth) const noexcept {

			const auto encoded_size = MeasureModifiedUtf8(value, depth);
			if (!encoded_size) return std::unexpected(encoded_size.error());
			return AddSizes(2, *encoded_size, depth);
		}

		[[nodiscard]] std::expected<void, NbtWriteError> ValidateContainerCount(
			std::size_t count,
			std::size_t configured_limit,
			NbtWriteErrorCode limit_error,
			std::size_t depth) const noexcept {

			if (count > configured_limit)
				return std::unexpected(MakeWriteError(limit_error, depth));
			if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::ContainerLengthExceeded, depth));
			return {};
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasureDocument(const NbtDocument& document) const noexcept {
			const auto name_size = MeasureString(document.Name, 0);
			if (!name_size) return std::unexpected(name_size.error());
			const auto payload_size = MeasurePayload(document.Root, 0);
			if (!payload_size) return std::unexpected(payload_size.error());

			const auto header_size = AddSizes(1, *name_size, 0);
			if (!header_size) return std::unexpected(header_size.error());
			return AddSizes(*header_size, *payload_size, 0);
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasureList(
			const NbtList& list,
			std::size_t depth) const noexcept {

			if (depth > m_Limits.MaximumDepth)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::DepthLimitExceeded, depth));
			if (!IsKnownNbtType(list.GetElementType()))
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidList, depth));
			if (!list.Empty() && list.GetElementType() == NbtType::End)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidList, depth));

			const auto count_valid = ValidateContainerCount(
				list.Size(),
				m_Limits.MaximumListElements,
				NbtWriteErrorCode::ListElementLimitExceeded,
				depth);
			if (!count_valid) return std::unexpected(count_valid.error());

			std::size_t result{ 5 };
			for (const auto& value : list.GetValues()) {
				if (value.GetType() != list.GetElementType())
					return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidList, depth));

				const auto payload_size = MeasurePayload(value, depth + 1);
				if (!payload_size) return std::unexpected(payload_size.error());
				const auto accumulated = AddSizes(result, *payload_size, depth);
				if (!accumulated) return std::unexpected(accumulated.error());
				result = *accumulated;
			}
			return result;
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasureCompound(
			const NbtCompound& compound,
			std::size_t depth) const noexcept {

			if (depth > m_Limits.MaximumDepth)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::DepthLimitExceeded, depth));
			if (compound.Size() > m_Limits.MaximumCompoundEntries)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::CompoundEntryLimitExceeded, depth));

			std::size_t result{ 1 };
			for (const auto& entry : compound.GetEntries()) {
				const auto name_size = MeasureString(entry.Name, depth);
				if (!name_size) return std::unexpected(name_size.error());
				const auto payload_size = MeasurePayload(entry.Value, depth + 1);
				if (!payload_size) return std::unexpected(payload_size.error());

				const auto with_type = AddSizes(result, 1, depth);
				if (!with_type) return std::unexpected(with_type.error());
				const auto with_name = AddSizes(*with_type, *name_size, depth);
				if (!with_name) return std::unexpected(with_name.error());
				const auto with_payload = AddSizes(*with_name, *payload_size, depth);
				if (!with_payload) return std::unexpected(with_payload.error());
				result = *with_payload;
			}
			return result;
		}

		[[nodiscard]] std::expected<std::size_t, NbtWriteError> MeasurePayload(
			const NbtValue& value,
			std::size_t depth) const noexcept {

			if (depth > m_Limits.MaximumDepth)
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::DepthLimitExceeded, depth));

			switch (value.GetType()) {
			case NbtType::Byte: return 1;
			case NbtType::Short: return 2;
			case NbtType::Int: return 4;
			case NbtType::Long: return 8;
			case NbtType::Float: return 4;
			case NbtType::Double: return 8;
			case NbtType::ByteArray: {
				const auto& array = *value.AsByteArray();
				const auto count_valid = ValidateContainerCount(
					array.size(),
					m_Limits.MaximumArrayElements,
					NbtWriteErrorCode::ArrayElementLimitExceeded,
					depth);
				if (!count_valid) return std::unexpected(count_valid.error());
				return AddSizes(4, array.size(), depth);
			}
			case NbtType::String: return MeasureString(*value.AsString(), depth);
			case NbtType::List: return MeasureList(*value.AsList(), depth);
			case NbtType::Compound: return MeasureCompound(*value.AsCompound(), depth);
			case NbtType::IntArray: {
				const auto& array = *value.AsIntArray();
				const auto count_valid = ValidateContainerCount(
					array.size(),
					m_Limits.MaximumArrayElements,
					NbtWriteErrorCode::ArrayElementLimitExceeded,
					depth);
				if (!count_valid) return std::unexpected(count_valid.error());
				const auto bytes = MultiplySizes(array.size(), sizeof(std::int32_t), depth);
				if (!bytes) return std::unexpected(bytes.error());
				return AddSizes(4, *bytes, depth);
			}
			case NbtType::LongArray: {
				const auto& array = *value.AsLongArray();
				const auto count_valid = ValidateContainerCount(
					array.size(),
					m_Limits.MaximumArrayElements,
					NbtWriteErrorCode::ArrayElementLimitExceeded,
					depth);
				if (!count_valid) return std::unexpected(count_valid.error());
				const auto bytes = MultiplySizes(array.size(), sizeof(std::int64_t), depth);
				if (!bytes) return std::unexpected(bytes.error());
				return AddSizes(4, *bytes, depth);
			}
			case NbtType::End:
				return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidRootType, depth));
			}
			return std::unexpected(MakeWriteError(NbtWriteErrorCode::InvalidRootType, depth));
		}

		void WriteUnsignedByte(std::uint8_t value) {
			m_Data.push_back(static_cast<std::byte>(value));
		}

		void WriteByte(std::int8_t value) {
			WriteUnsignedByte(std::bit_cast<std::uint8_t>(value));
		}

		void WriteUnsignedShort(std::uint16_t value) {
			WriteUnsignedByte(static_cast<std::uint8_t>(value >> 8));
			WriteUnsignedByte(static_cast<std::uint8_t>(value));
		}

		void WriteShort(std::int16_t value) {
			WriteUnsignedShort(std::bit_cast<std::uint16_t>(value));
		}

		void WriteUnsignedInt(std::uint32_t value) {
			WriteUnsignedByte(static_cast<std::uint8_t>(value >> 24));
			WriteUnsignedByte(static_cast<std::uint8_t>(value >> 16));
			WriteUnsignedByte(static_cast<std::uint8_t>(value >> 8));
			WriteUnsignedByte(static_cast<std::uint8_t>(value));
		}

		void WriteInt(std::int32_t value) {
			WriteUnsignedInt(std::bit_cast<std::uint32_t>(value));
		}

		void WriteUnsignedLong(std::uint64_t value) {
			for (int shift{ 56 }; shift >= 0; shift -= 8)
				WriteUnsignedByte(static_cast<std::uint8_t>(value >> shift));
		}

		void WriteLong(std::int64_t value) {
			WriteUnsignedLong(std::bit_cast<std::uint64_t>(value));
		}

		void WriteFloat(float value) {
			WriteUnsignedInt(std::bit_cast<std::uint32_t>(value));
		}

		void WriteDouble(double value) {
			WriteUnsignedLong(std::bit_cast<std::uint64_t>(value));
		}

		void WriteString(const NbtString& value) {
			std::size_t encoded_size{ 0 };
			for (const auto code_unit : value.GetUtf16())
				encoded_size += code_unit >= 0x0001u && code_unit <= 0x007Fu ? 1
				: code_unit <= 0x07FFu ? 2
				: 3;

			WriteUnsignedShort(static_cast<std::uint16_t>(encoded_size));
			for (const auto code_unit : value.GetUtf16()) {
				if (code_unit >= 0x0001u && code_unit <= 0x007Fu)
					WriteUnsignedByte(static_cast<std::uint8_t>(code_unit));
				else if (code_unit <= 0x07FFu) {
					WriteUnsignedByte(static_cast<std::uint8_t>(0xC0u | (code_unit >> 6)));
					WriteUnsignedByte(static_cast<std::uint8_t>(0x80u | (code_unit & 0x3Fu)));
				}
				else {
					WriteUnsignedByte(static_cast<std::uint8_t>(0xE0u | (code_unit >> 12)));
					WriteUnsignedByte(static_cast<std::uint8_t>(0x80u | ((code_unit >> 6) & 0x3Fu)));
					WriteUnsignedByte(static_cast<std::uint8_t>(0x80u | (code_unit & 0x3Fu)));
				}
			}
		}

		void WriteList(const NbtList& list) {
			WriteUnsignedByte(static_cast<std::uint8_t>(list.GetElementType()));
			WriteInt(static_cast<std::int32_t>(list.Size()));
			for (const auto& value : list.GetValues()) WritePayload(value);
		}

		void WriteCompound(const NbtCompound& compound) {
			for (const auto& entry : compound.GetEntries()) {
				WriteUnsignedByte(static_cast<std::uint8_t>(entry.Value.GetType()));
				WriteString(entry.Name);
				WritePayload(entry.Value);
			}
			WriteUnsignedByte(static_cast<std::uint8_t>(NbtType::End));
		}

		void WritePayload(const NbtValue& value) {
			switch (value.GetType()) {
			case NbtType::Byte: WriteByte(*value.AsByte()); break;
			case NbtType::Short: WriteShort(*value.AsShort()); break;
			case NbtType::Int: WriteInt(*value.AsInt()); break;
			case NbtType::Long: WriteLong(*value.AsLong()); break;
			case NbtType::Float: WriteFloat(*value.AsFloat()); break;
			case NbtType::Double: WriteDouble(*value.AsDouble()); break;
			case NbtType::ByteArray:
				WriteInt(static_cast<std::int32_t>(value.AsByteArray()->size()));
				for (const auto element : *value.AsByteArray()) WriteByte(element);
				break;
			case NbtType::String: WriteString(*value.AsString()); break;
			case NbtType::List: WriteList(*value.AsList()); break;
			case NbtType::Compound: WriteCompound(*value.AsCompound()); break;
			case NbtType::IntArray:
				WriteInt(static_cast<std::int32_t>(value.AsIntArray()->size()));
				for (const auto element : *value.AsIntArray()) WriteInt(element);
				break;
			case NbtType::LongArray:
				WriteInt(static_cast<std::int32_t>(value.AsLongArray()->size()));
				for (const auto element : *value.AsLongArray()) WriteLong(element);
				break;
			case NbtType::End: break;
			}
		}

		NbtLimits m_Limits;
		std::vector<std::byte> m_Data;
	};
}

namespace Aurore::Util {
	std::expected<NbtDecoded<NbtDocument>, NbtReadError> NbtReader::ReadDocument(std::span<const std::byte> data, NbtLimits limits) {
		NbtDecoder decoder{ data, limits };
		const auto document = decoder.ReadDocument();
		if (!document) return std::unexpected(document.error());
		return NbtDecoded<NbtDocument>{
			.Value = std::move(document).value(),
				.BytesConsumed = decoder.Position(),
		};
	}

	std::expected<NbtDecoded<NbtCompound>, NbtReadError> NbtReader::ReadNetworkCompound(std::span<const std::byte> data, NbtLimits limits) {
		NbtDecoder decoder{ data, limits };
		const auto compound = decoder.ReadNetworkCompound();
		if (!compound) return std::unexpected(compound.error());
		return NbtDecoded<NbtCompound>{
			.Value = std::move(compound).value(),
			.BytesConsumed = decoder.Position()
		};
	}

	std::expected<std::vector<std::byte>, NbtWriteError> NbtWriter::WriteDocument(const NbtDocument& document, NbtLimits limits) {
		NbtEncoder encoder{ limits };
		return encoder.WriteDocument(document);
	}

	std::expected<std::vector<std::byte>, NbtWriteError> NbtWriter::WriteNetworkCompound(const NbtCompound& compound, NbtLimits limits) {
		NbtEncoder encoder{ limits };
		return encoder.WriteNetworkCompound(compound);
	}
}
