#include <Aurore/Util/Nbt.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace {
	using Aurore::Util::NbtTextError;
	using Aurore::Util::NbtTextErrorCode;

	constexpr bool IsContinuationByte(std::uint8_t value) noexcept {
		return (value & 0xC0u) == 0x80u;
	}
}

namespace Aurore::Util {
	NbtString::NbtString(std::u16string value) noexcept
		: m_Value(std::move(value)) {}

	std::expected<NbtString, NbtTextError> NbtString::FromUtf8(std::string_view value) {
		std::u16string result;
		result.reserve(value.size());

		for (std::size_t offset{ 0 }; offset < value.size();) {
			const auto first = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset]));
			std::uint32_t code_point{ 0 };
			std::size_t sequence_length{ 0 };

			if (first <= 0x7Fu) {
				code_point = first;
				sequence_length = 1;
			}
			else if (first >= 0xC2u && first <= 0xDFu) {
				sequence_length = 2;
				if (offset + sequence_length > value.size())
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

				const auto second = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 1]));
				if (!IsContinuationByte(second))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 1 });

				code_point = (static_cast<std::uint32_t>(first & 0x1Fu) << 6)
					| static_cast<std::uint32_t>(second & 0x3Fu);
			}
			else if (first >= 0xE0u && first <= 0xEFu) {
				sequence_length = 3;
				if (offset + sequence_length > value.size())
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

				const auto second = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 1]));
				const auto third = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 2]));
				if (!IsContinuationByte(second))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 1 });
				if (!IsContinuationByte(third))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 2 });
				if (first == 0xE0u && second < 0xA0u)
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });
				if (first == 0xEDu && second >= 0xA0u)
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

				code_point = (static_cast<std::uint32_t>(first & 0x0Fu) << 12)
					| (static_cast<std::uint32_t>(second & 0x3Fu) << 6)
					| static_cast<std::uint32_t>(third & 0x3Fu);
			}
			else if (first >= 0xF0u && first <= 0xF4u) {
				sequence_length = 4;
				if (offset + sequence_length > value.size())
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

				const auto second = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 1]));
				const auto third = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 2]));
				const auto fourth = static_cast<std::uint8_t>(static_cast<unsigned char>(value[offset + 3]));
				if (!IsContinuationByte(second))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 1 });
				if (!IsContinuationByte(third))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 2 });
				if (!IsContinuationByte(fourth))
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset + 3 });
				if (first == 0xF0u && second < 0x90u)
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });
				if (first == 0xF4u && second > 0x8Fu)
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

				code_point = (static_cast<std::uint32_t>(first & 0x07u) << 18)
					| (static_cast<std::uint32_t>(second & 0x3Fu) << 12)
					| (static_cast<std::uint32_t>(third & 0x3Fu) << 6)
					| static_cast<std::uint32_t>(fourth & 0x3Fu);
			}
			else return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::InvalidUtf8, .Offset = offset });

			if (code_point <= 0xFFFFu) result.push_back(static_cast<char16_t>(code_point));
			else {
				code_point -= 0x10000u;
				result.push_back(static_cast<char16_t>(0xD800u + (code_point >> 10)));
				result.push_back(static_cast<char16_t>(0xDC00u + (code_point & 0x3FFu)));
			}

			offset += sequence_length;
		}

		return NbtString{ std::move(result) };
	}

	std::expected<std::string, NbtTextError> NbtString::ToUtf8() const {
		std::string result;
		result.reserve(m_Value.size());

		for (std::size_t index{ 0 }; index < m_Value.size(); ++index) {
			std::uint32_t code_point = m_Value[index];
			if (code_point >= 0xD800u && code_point <= 0xDBFFu) {
				if (index + 1 >= m_Value.size())
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::UnpairedSurrogate, .Offset = index });

				const auto low = static_cast<std::uint32_t>(m_Value[index + 1]);
				if (low < 0xDC00u || low > 0xDFFFu)
					return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::UnpairedSurrogate, .Offset = index });

				code_point = 0x10000u + ((code_point - 0xD800u) << 10) + (low - 0xDC00u);
				++index;
			}
			else if (code_point >= 0xDC00u && code_point <= 0xDFFFu)
				return std::unexpected(NbtTextError{ .Code = NbtTextErrorCode::UnpairedSurrogate, .Offset = index });

			if (code_point <= 0x7Fu) result.push_back(static_cast<char>(code_point));
			else if (code_point <= 0x7FFu) {
				result.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
				result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
			}
			else if (code_point <= 0xFFFFu) {
				result.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
				result.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
				result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
			}
			else {
				result.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
				result.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
				result.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
				result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
			}
		}

		return result;
	}

	NbtValue::NbtValue(Storage value) noexcept
		: m_Value(std::move(value)) {}

	NbtValue NbtValue::Byte(std::int8_t value) {
		return NbtValue{ Storage{ std::in_place_type<std::int8_t>, value } };
	}

	NbtValue NbtValue::Short(std::int16_t value) {
		return NbtValue{ Storage{ std::in_place_type<std::int16_t>, value } };
	}

	NbtValue NbtValue::Int(std::int32_t value) {
		return NbtValue{ Storage{ std::in_place_type<std::int32_t>, value } };
	}

	NbtValue NbtValue::Long(std::int64_t value) {
		return NbtValue{ Storage{ std::in_place_type<std::int64_t>, value } };
	}

	NbtValue NbtValue::Float(float value) {
		return NbtValue{ Storage{ std::in_place_type<float>, value } };
	}

	NbtValue NbtValue::Double(double value) {
		return NbtValue{ Storage{ std::in_place_type<double>, value } };
	}

	NbtValue NbtValue::ByteArray(NbtByteArray value) {
		return NbtValue{ Storage{ std::in_place_type<NbtByteArray>, std::move(value) } };
	}

	NbtValue NbtValue::String(NbtString value) {
		return NbtValue{ Storage{ std::in_place_type<NbtString>, std::move(value) } };
	}

	NbtValue NbtValue::List(NbtList value) {
		return NbtValue{ Storage{ std::in_place_type<ListStorage>, std::make_shared<const NbtList>(std::move(value)) } };
	}

	NbtValue NbtValue::Compound(NbtCompound value) {
		return NbtValue{ Storage{ std::in_place_type<CompoundStorage>, std::make_shared<const NbtCompound>(std::move(value)) } };
	}

	NbtValue NbtValue::IntArray(NbtIntArray value) {
		return NbtValue{ Storage{ std::in_place_type<NbtIntArray>, std::move(value) } };
	}

	NbtValue NbtValue::LongArray(NbtLongArray value) {
		return NbtValue{ Storage{ std::in_place_type<NbtLongArray>, std::move(value) } };
	}

	NbtType NbtValue::GetType() const noexcept {
		return static_cast<NbtType>(m_Value.index() + 1);
	}

	const std::int8_t* NbtValue::AsByte() const noexcept { return std::get_if<std::int8_t>(&m_Value); }
	const std::int16_t* NbtValue::AsShort() const noexcept { return std::get_if<std::int16_t>(&m_Value); }
	const std::int32_t* NbtValue::AsInt() const noexcept { return std::get_if<std::int32_t>(&m_Value); }
	const std::int64_t* NbtValue::AsLong() const noexcept { return std::get_if<std::int64_t>(&m_Value); }
	const float* NbtValue::AsFloat() const noexcept { return std::get_if<float>(&m_Value); }
	const double* NbtValue::AsDouble() const noexcept { return std::get_if<double>(&m_Value); }
	const NbtByteArray* NbtValue::AsByteArray() const noexcept { return std::get_if<NbtByteArray>(&m_Value); }
	const NbtString* NbtValue::AsString() const noexcept { return std::get_if<NbtString>(&m_Value); }
	const NbtIntArray* NbtValue::AsIntArray() const noexcept { return std::get_if<NbtIntArray>(&m_Value); }
	const NbtLongArray* NbtValue::AsLongArray() const noexcept { return std::get_if<NbtLongArray>(&m_Value); }

	const NbtList* NbtValue::AsList() const noexcept {
		const auto value = std::get_if<ListStorage>(&m_Value);
		return value == nullptr ? nullptr : value->get();
	}

	const NbtCompound* NbtValue::AsCompound() const noexcept {
		const auto value = std::get_if<CompoundStorage>(&m_Value);
		return value == nullptr ? nullptr : value->get();
	}

	bool NbtValue::ExactEquals(const NbtValue& other) const noexcept {
		if (GetType() != other.GetType()) return false;

		switch (GetType()) {
		case NbtType::Byte: return *AsByte() == *other.AsByte();
		case NbtType::Short: return *AsShort() == *other.AsShort();
		case NbtType::Int: return *AsInt() == *other.AsInt();
		case NbtType::Long: return *AsLong() == *other.AsLong();
		case NbtType::Float:
			return std::bit_cast<std::uint32_t>(*AsFloat()) == std::bit_cast<std::uint32_t>(*other.AsFloat());
		case NbtType::Double:
			return std::bit_cast<std::uint64_t>(*AsDouble()) == std::bit_cast<std::uint64_t>(*other.AsDouble());
		case NbtType::ByteArray: return *AsByteArray() == *other.AsByteArray();
		case NbtType::String: return *AsString() == *other.AsString();
		case NbtType::List: return AsList()->ExactEquals(*other.AsList());
		case NbtType::Compound: return AsCompound()->ExactEquals(*other.AsCompound());
		case NbtType::IntArray: return *AsIntArray() == *other.AsIntArray();
		case NbtType::LongArray: return *AsLongArray() == *other.AsLongArray();
		case NbtType::End: break;
		}
		return false;
	}

	NbtList::NbtList(NbtType elements_type, std::vector<NbtValue> values) noexcept
		: m_ElementType(elements_type), m_Values(std::move(values)) {}

	std::expected<NbtList, NbtModelError> NbtList::Create(NbtType element_type, std::vector<NbtValue> values) {
		if (!IsKnownNbtType(element_type))
			return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::UnknownListElementType, .Index = 0 });

		if (!values.empty() && element_type == NbtType::End)
			return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::EndListElementTypeWithValues, .Index = 0 });

		for (std::size_t index{ 0 }; index < values.size(); ++index)
			if (values[index].GetType() != element_type)
				return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::MismatchedListElementType, .Index = index });

		return NbtList{ element_type, std::move(values) };
	}

	std::expected<void, NbtModelError> NbtList::Append(NbtValue value) {
		if (!IsKnownNbtType(m_ElementType))
			return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::UnknownListElementType, .Index = m_Values.size() });
		if (m_ElementType == NbtType::End)
			return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::EndListElementTypeWithValues, .Index = m_Values.size() });
		if (value.GetType() != m_ElementType)
			return std::unexpected(NbtModelError{ .Code = NbtModelErrorCode::MismatchedListElementType, .Index = m_Values.size() });

		m_Values.push_back(std::move(value));
		return {};
	}

	bool NbtList::ExactEquals(const NbtList& other) const noexcept {
		if (m_ElementType != other.m_ElementType || m_Values.size() != other.m_Values.size()) return false;
		for (std::size_t index{ 0 }; index < m_Values.size(); ++index)
			if (!m_Values[index].ExactEquals(other.m_Values[index])) return false;
		return true;
	}

	bool NbtNamedValue::ExactEquals(const NbtNamedValue& other) const noexcept {
		return Name == other.Name && Value.ExactEquals(other.Value);
	}

	void NbtCompound::Set(NbtString name, NbtValue value) {
		for (auto& entry : m_Entries) {
			if (entry.Name != name) continue;
			entry.Value = std::move(value);
			return;
		}

		m_Entries.push_back(NbtNamedValue{ .Name = std::move(name), .Value = std::move(value) });
	}

	bool NbtCompound::Remove(std::u16string_view name) {
		const auto iterator = std::find_if(m_Entries.begin(), m_Entries.end(), [name](const NbtNamedValue& entry) {
			return entry.Name.GetUtf16() == name;
		});
		if (iterator == m_Entries.end()) return false;

		m_Entries.erase(iterator);
		return true;
	}

	NbtValue* NbtCompound::Find(std::u16string_view name) noexcept {
		for (auto& entry : m_Entries)
			if (entry.Name.GetUtf16() == name) return &entry.Value;
		return nullptr;
	}

	const NbtValue* NbtCompound::Find(std::u16string_view name) const noexcept {
		for (const auto& entry : m_Entries)
			if (entry.Name.GetUtf16() == name) return &entry.Value;
		return nullptr;
	}

	bool NbtCompound::ExactEquals(const NbtCompound& other) const noexcept {
		if (m_Entries.size() != other.m_Entries.size()) return false;
		for (std::size_t index{ 0 }; index < m_Entries.size(); ++index)
			if (!m_Entries[index].ExactEquals(other.m_Entries[index])) return false;
		return true;
	}

	bool NbtDocument::ExactEquals(const NbtDocument& other) const noexcept {
		return Name == other.Name && Root.ExactEquals(other.Root);
	}
}
