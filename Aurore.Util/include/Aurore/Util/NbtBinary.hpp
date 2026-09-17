#pragma once

#include "Nbt.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace Aurore::Util {
	struct NbtLimits final {
		std::size_t MaximumTotalBytes{ 16 * 1024 * 1024 };
		std::size_t MaximumDepth{ 64 };
		std::size_t MaximumStringBytes{ 65'535 };
		std::size_t MaximumArrayElements{ 1'048'576 };
		std::size_t MaximumListElements{ 1'048'576 };
		std::size_t MaximumCompoundEntries{ 65'536 };
	};

	enum class NbtReadErrorCode : std::uint8_t {
		IncompleteData,
		UnknownTagType,
		InvalidRootType,
		UnexpectedEndTag,
		InvalidListElementType,
		NegativeLength,
		InvalidModifiedUtf8,
		TotalByteLimitExceeded,
		DepthLimitExceeded,
		StringByteLimitExceeded,
		ArrayElementLimitExceeded,
		ListElementLimitExceeded,
		CompoundEntryLimitExceeded,
		AllocationSizeOverflow,
	};

	struct NbtReadError final {
		NbtReadErrorCode Code;
		std::size_t Offset{ 0 };
		std::size_t Depth{ 0 };

		auto operator<=>(const NbtReadError&) const noexcept = default;
	};

	enum class NbtWriteErrorCode : std::uint8_t {
		InvalidRootType,
		InvalidList,
		StringByteLimitExceeded,
		TotalByteLimitExceeded,
		DepthLimitExceeded,
		ArrayElementLimitExceeded,
		ListElementLimitExceeded,
		CompoundEntryLimitExceeded,
		ContainerLengthExceeded,
		AllocationSizeOverflow,
	};

	struct NbtWriteError final {
		NbtWriteErrorCode Code;
		std::size_t Depth{ 0 };

		auto operator<=>(const NbtWriteError&) const noexcept = default;
	};

	template<typename T>
	struct NbtDecoded final {
		T Value;
		std::size_t BytesConsumed{ 0 };
	};

	class NbtReader final {
	public:
		[[nodiscard]] static std::expected<NbtDecoded<NbtDocument>, NbtReadError> ReadDocument(std::span<const std::byte> data, NbtLimits limits = {});
		[[nodiscard]] static std::expected<NbtDecoded<NbtCompound>, NbtReadError> ReadNetworkCompound(std::span<const std::byte> data, NbtLimits limits = {});
	};

	class NbtWriter final {
	public:
		[[nodiscard]] static std::expected<std::vector<std::byte>, NbtWriteError> WriteDocument(const NbtDocument& document, NbtLimits limits = {});
		[[nodiscard]] static std::expected<std::vector<std::byte>, NbtWriteError> WriteNetworkCompound(const NbtCompound& compound, NbtLimits limits = {});
	};
}
