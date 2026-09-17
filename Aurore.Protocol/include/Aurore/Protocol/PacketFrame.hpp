#pragma once

#include <Aurore/Util/ByteBuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace Aurore::Protocol {
	enum class FrameError : std::uint8_t {
		MalformedLength,
		NegativeLength,
		PacketTooLarge,
		MissingPacketId,
		MalformedPacketId,
	};

	template<typename T>
	using FrameResult = std::expected<T, FrameError>;

	struct PacketFrame final {
		std::int32_t PacketId{ 0 };
		std::vector<std::byte> Payload;
	};

	[[nodiscard]] Aurore::Util::ByteBuffer EncodePacketData(std::int32_t packet_id, std::span<const std::byte> payload);
	[[nodiscard]] FrameResult<PacketFrame> DecodePacketData(std::span<const std::byte> packet_data);
	[[nodiscard]] Aurore::Util::ByteBuffer EncodeLengthPrefixedPacketData(std::span<const std::byte> packet_data);
	[[nodiscard]] Aurore::Util::ByteBuffer EncodePacketFrame(std::int32_t packet_id, std::span<const std::byte> payload);

	class PacketStreamDecoder final {
	public:
		static constexpr std::size_t DefaultMaximumPacketSize{ 2 * 1024 * 1024 };

		explicit PacketStreamDecoder(std::size_t maximum_packet_size = DefaultMaximumPacketSize);

		void Append(std::span<const std::byte> bytes);

		[[nodiscard]] FrameResult<std::optional<std::vector<std::byte>>> TryDecode();

		void Clear() noexcept;

		[[nodiscard]] std::size_t BufferedBytes() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t GetMaximumPacketSize() const noexcept;

	private:
		void Compact();

		std::vector<std::byte> m_Buffer;
		std::size_t m_ReadPosition{ 0 };
		std::size_t m_MaximumPacketSize;
	};

	class PacketFrameDecoder final {
	public:
		static constexpr std::size_t DefaultMaximumPacketSize{ PacketStreamDecoder::DefaultMaximumPacketSize };

		explicit PacketFrameDecoder(std::size_t maximum_packet_size = DefaultMaximumPacketSize);

		void Append(std::span<const std::byte> bytes);

		[[nodiscard]] FrameResult<std::optional<PacketFrame>> TryDecode();

		void Clear() noexcept;

		[[nodiscard]] std::size_t BufferedBytes() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t GetMaximumPacketSize() const noexcept;

	private:
		PacketStreamDecoder m_StreamDecoder;
	};
}
