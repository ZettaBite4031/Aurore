#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Aurore::Util {
	enum class NbtType : std::uint8_t {
		End = 0,
		Byte = 1,
		Short = 2,
		Int = 3,
		Long = 4,
		Float = 5,
		Double = 6,
		ByteArray = 7,
		String = 8,
		List = 9,
		Compound = 10,
		IntArray = 11,
		LongArray = 12,
	};

	[[nodiscard]] constexpr bool IsKnownNbtType(NbtType type) noexcept {
		return static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(NbtType::LongArray);
	}

	enum class NbtTextErrorCode : std::uint8_t {
		InvalidUtf8,
		UnpairedSurrogate,
	};

	struct NbtTextError final {
		NbtTextErrorCode Code;
		std::size_t Offset{ 0 };

		auto operator<=>(const NbtTextError&) const noexcept = default;
	};

	class NbtString final {
	public:
		NbtString() = default;
		explicit NbtString(std::u16string value) noexcept;

		[[nodiscard]] static std::expected<NbtString, NbtTextError> FromUtf8(std::string_view value);
		[[nodiscard]] std::expected<std::string, NbtTextError> ToUtf8() const;

		[[nodiscard]] std::u16string_view GetUtf16() const noexcept { return m_Value; }
		[[nodiscard]] bool Empty() const noexcept { return m_Value.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Value.size(); }

		auto operator<=>(const NbtString&) const noexcept = default;

	private:
		std::u16string m_Value;
	};

	class NbtList;
	class NbtCompound;

	using NbtByteArray = std::vector<std::int8_t>;
	using NbtIntArray = std::vector<std::int32_t>;
	using NbtLongArray = std::vector<std::int64_t>;

	class NbtValue final {
	public:
		NbtValue(const NbtValue&) = default;
		NbtValue(NbtValue&&) noexcept = default;
		NbtValue& operator=(const NbtValue&) = default;
		NbtValue& operator=(NbtValue&&) noexcept = default;
		~NbtValue() = default;

		[[nodiscard]] static NbtValue Byte(std::int8_t value);
		[[nodiscard]] static NbtValue Short(std::int16_t value);
		[[nodiscard]] static NbtValue Int(std::int32_t value);
		[[nodiscard]] static NbtValue Long(std::int64_t value);
		[[nodiscard]] static NbtValue Float(float value);
		[[nodiscard]] static NbtValue Double(double value);
		[[nodiscard]] static NbtValue ByteArray(NbtByteArray value);
		[[nodiscard]] static NbtValue String(NbtString value);
		[[nodiscard]] static NbtValue List(NbtList value);
		[[nodiscard]] static NbtValue Compound(NbtCompound value);
		[[nodiscard]] static NbtValue IntArray(NbtIntArray value);
		[[nodiscard]] static NbtValue LongArray(NbtLongArray value);

		[[nodiscard]] NbtType GetType() const noexcept;

		[[nodiscard]] const std::int8_t* AsByte() const noexcept;
		[[nodiscard]] const std::int16_t* AsShort() const noexcept;
		[[nodiscard]] const std::int32_t* AsInt() const noexcept;
		[[nodiscard]] const std::int64_t* AsLong() const noexcept;
		[[nodiscard]] const float* AsFloat() const noexcept;
		[[nodiscard]] const double* AsDouble() const noexcept;
		[[nodiscard]] const NbtByteArray* AsByteArray() const noexcept;
		[[nodiscard]] const NbtString* AsString() const noexcept;
		[[nodiscard]] const NbtList* AsList() const noexcept;
		[[nodiscard]] const NbtCompound* AsCompound() const noexcept;
		[[nodiscard]] const NbtIntArray* AsIntArray() const noexcept;
		[[nodiscard]] const NbtLongArray* AsLongArray() const noexcept;

		[[nodiscard]] bool ExactEquals(const NbtValue& other) const noexcept;
		[[nodiscard]] bool operator==(const NbtValue& other) const noexcept { return ExactEquals(other); }

	private:
		using ListStorage = std::shared_ptr<const NbtList>;
		using CompoundStorage = std::shared_ptr<const NbtCompound>;
		using Storage = std::variant<
			std::int8_t,
			std::int16_t,
			std::int32_t,
			std::int64_t,
			float,
			double,
			NbtByteArray,
			NbtString,
			ListStorage,
			CompoundStorage,
			NbtIntArray,
			NbtLongArray>;

		explicit NbtValue(Storage value) noexcept;

		Storage m_Value;
	};

	enum class NbtModelErrorCode : std::uint8_t {
		UnknownListElementType,
		EndListElementTypeWithValues,
		MismatchedListElementType,
	};

	struct NbtModelError final {
		NbtModelErrorCode Code;
		std::size_t Index{ 0 };

		auto operator<=>(const NbtModelError&) const noexcept = default;
	};

	class NbtList final {
	public:
		NbtList() = default;

		[[nodiscard]] static std::expected<NbtList, NbtModelError> Create(NbtType elements_type, std::vector<NbtValue> values = {});
		[[nodiscard]] std::expected<void, NbtModelError> Append(NbtValue value);

		[[nodiscard]] NbtType GetElementType() const noexcept { return m_ElementType; }
		[[nodiscard]] bool Empty() const noexcept { return m_Values.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Values.size(); }
		[[nodiscard]] std::span<const NbtValue> GetValues() const noexcept { return m_Values; }

		[[nodiscard]] bool ExactEquals(const NbtList& other) const noexcept;
		[[nodiscard]] bool operator==(const NbtList& other) const noexcept { return ExactEquals(other); }

	private:
		NbtList(NbtType elements_type, std::vector<NbtValue> values) noexcept;

		NbtType m_ElementType{ NbtType::End };
		std::vector<NbtValue> m_Values;
	};

	struct NbtNamedValue final {
		NbtString Name;
		NbtValue Value;

		[[nodiscard]] bool ExactEquals(const NbtNamedValue& other) const noexcept;
		[[nodiscard]] bool operator==(const NbtNamedValue& other) const noexcept { return ExactEquals(other); }
	};

	class NbtCompound final {
	public:
		void Set(NbtString name, NbtValue value);
		[[nodiscard]] bool Remove(std::u16string_view name);

		[[nodiscard]] NbtValue* Find(std::u16string_view name) noexcept;
		[[nodiscard]] const NbtValue* Find(std::u16string_view name) const noexcept;
		[[nodiscard]] bool Contains(std::u16string_view name) const noexcept { return Find(name) != nullptr; }

		[[nodiscard]] bool Empty() const noexcept { return m_Entries.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }
		[[nodiscard]] std::span<const NbtNamedValue> GetEntries() const noexcept { return m_Entries; }

		[[nodiscard]] bool ExactEquals(const NbtCompound& other) const noexcept;
		[[nodiscard]] bool operator==(const NbtCompound& other) const noexcept { return ExactEquals(other); }

	private:
		std::vector<NbtNamedValue> m_Entries;
	};

	struct NbtDocument final {
		NbtString Name;
		NbtValue Root;

		[[nodiscard]] bool ExactEquals(const NbtDocument& other) const noexcept;
		[[nodiscard]] bool operator==(const NbtDocument& other) const noexcept { return ExactEquals(other); }
	};
}
