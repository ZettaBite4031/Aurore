#include <Aurore/Protocol/PacketFrame.hpp>

#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
	enum class VarIntStatus {
		Complete,
		Incomplete,
		Malformed,
	};

	struct DecodedVarInt {
		VarIntStatus Status{ VarIntStatus::Incomplete };
		std::int32_t Value{ 0 };
		std::size_t Size{ 0 };
	};

	DecodedVarInt DecodeVarIntPrefix(std::span<const std::byte> bytes) noexcept {
		std::uint32_t result{ 0 };

		for (std::size_t index = 0; index < 5; ++index) {
			if (index >= bytes.size()) {
				return { .Status = VarIntStatus::Incomplete };
			}

			const auto currentByte = std::to_integer<std::uint8_t>(bytes[index]);

			const auto payload = static_cast<std::uint32_t>(currentByte & 0x7Fu);

			/*
				A 32-bit VarInt only has four usable payload bits in its
				fifth byte.
			*/
			if (index == 4 && payload > 0x0Fu) {
				return { .Status = VarIntStatus::Malformed };
			}

			result |= payload << (index * 7);

			if ((currentByte & 0x80u) == 0) {
				return {
					.Status = VarIntStatus::Complete,
					.Value = std::bit_cast<std::int32_t>(result),
					.Size = index + 1
				};
			}
		}

		return { .Status = VarIntStatus::Malformed };
	}
}

namespace Aurore::Protocol {
	Aurore::Util::ByteBuffer EncodePacketData(std::int32_t packet_id, std::span<const std::byte> payload) {
		Aurore::Util::ByteBuffer packet_data;
		packet_data.Reserve(5 + payload.size());
		packet_data.WriteVarInt(packet_id);
		packet_data.WriteBytes(payload);
		if (packet_data.Size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
			throw std::length_error("Packet data is too large for a VarInt length");
		return packet_data;
	}

	FrameResult<PacketFrame> DecodePacketData(std::span<const std::byte> packet_data) {
		if (packet_data.empty())
			return std::unexpected(FrameError::MissingPacketId);

		const auto packet_id = DecodeVarIntPrefix(packet_data);
		if (packet_id.Status != VarIntStatus::Complete)
			return std::unexpected(FrameError::MalformedPacketId);

		const auto payload = packet_data.subspan(packet_id.Size);
		return PacketFrame{
			.PacketId = packet_id.Value,
			.Payload = std::vector<std::byte>(payload.begin(), payload.end()),
		};
	}

	Aurore::Util::ByteBuffer EncodeLengthPrefixedPacketData(std::span<const std::byte> packet_data) {
		if (packet_data.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
			throw std::length_error("Packet data is too large for a VarInt length");

		Aurore::Util::ByteBuffer frame;
		frame.Reserve(5 + packet_data.size());
		frame.WriteVarInt(static_cast<std::int32_t>(packet_data.size()));
		frame.WriteBytes(packet_data);
		return frame;
	}

	Aurore::Util::ByteBuffer EncodePacketFrame(std::int32_t packet_id, std::span<const std::byte> payload) {
		const auto packet_data = EncodePacketData(packet_id, payload);
		return EncodeLengthPrefixedPacketData(packet_data.Bytes());
	}

	PacketStreamDecoder::PacketStreamDecoder(std::size_t maximum_packet_size)
		: m_MaximumPacketSize(maximum_packet_size) {
		if (m_MaximumPacketSize == 0)
			throw std::invalid_argument("Maximum packet size must be greater than zero!");
	}

	void PacketStreamDecoder::Append(std::span<const std::byte> bytes) {
		if (bytes.empty()) return;
		m_Buffer.insert(m_Buffer.end(), bytes.begin(), bytes.end());
	}

	FrameResult<std::optional<std::vector<std::byte>>> PacketStreamDecoder::TryDecode() {
		if (Empty())
			return std::optional<std::vector<std::byte>>{};

		const auto available = std::span<const std::byte>{ m_Buffer.data() + m_ReadPosition, m_Buffer.size() - m_ReadPosition };
		const auto packet_length = DecodeVarIntPrefix(available);
		if (packet_length.Status == VarIntStatus::Incomplete)
			return std::optional<std::vector<std::byte>>{};
		if (packet_length.Status == VarIntStatus::Malformed)
			return std::unexpected(FrameError::MalformedLength);
		if (packet_length.Value < 0)
			return std::unexpected(FrameError::NegativeLength);

		const auto body_length = static_cast<std::size_t>(packet_length.Value);
		if (body_length > m_MaximumPacketSize)
			return std::unexpected(FrameError::PacketTooLarge);

		const auto bytes_after_length = available.size() - packet_length.Size;
		if (bytes_after_length < body_length)
			return std::optional<std::vector<std::byte>>{};

		const auto packet_data = available.subspan(packet_length.Size, body_length);
		std::vector<std::byte> result(packet_data.begin(), packet_data.end());

		m_ReadPosition += packet_length.Size + body_length;
		Compact();
		return std::optional<std::vector<std::byte>>{ std::move(result) };
	}

	void PacketStreamDecoder::Clear() noexcept {
		m_Buffer.clear();
		m_ReadPosition = 0;
	}

	std::size_t PacketStreamDecoder::BufferedBytes() const noexcept {
		return m_Buffer.size() - m_ReadPosition;
	}

	bool PacketStreamDecoder::Empty() const noexcept {
		return BufferedBytes() == 0;
	}

	std::size_t PacketStreamDecoder::GetMaximumPacketSize() const noexcept {
		return m_MaximumPacketSize;
	}

	void PacketStreamDecoder::Compact() {
		if (m_ReadPosition == 0) return;
		if (m_ReadPosition == m_Buffer.size()) {
			Clear();
			return;
		}

		/*
			Avoid moving remaining bytes after every decoded packet. Compact only
			when the consumed prefix is substantial and occupies at least half of
			the active allocation.
		*/
		if (m_ReadPosition < 4096 || m_ReadPosition < m_Buffer.size() / 2) return;

		using DifferenceType = std::vector<std::byte>::difference_type;
		m_Buffer.erase(m_Buffer.begin(), m_Buffer.begin() + static_cast<DifferenceType>(m_ReadPosition));
		m_ReadPosition = 0;
	}

	PacketFrameDecoder::PacketFrameDecoder(std::size_t maximum_packet_size)
		: m_StreamDecoder(maximum_packet_size) {}

	void PacketFrameDecoder::Append(std::span<const std::byte> bytes) {
		if (bytes.empty()) return;
		m_StreamDecoder.Append(bytes);
	}

	FrameResult<std::optional<PacketFrame>> PacketFrameDecoder::TryDecode() {
		auto packet_data = m_StreamDecoder.TryDecode();
		if (!packet_data)
			return std::unexpected(packet_data.error());
		if (!packet_data->has_value())
			return std::optional<PacketFrame>{};

		auto frame = DecodePacketData(packet_data->value());
		if (!frame)
			return std::unexpected(frame.error());
		return std::optional<PacketFrame>{ std::move(*frame) };
	}

	void PacketFrameDecoder::Clear() noexcept {
		m_StreamDecoder.Clear();
	}

	std::size_t PacketFrameDecoder::BufferedBytes() const noexcept {
		return m_StreamDecoder.BufferedBytes();
	}

	bool PacketFrameDecoder::Empty() const noexcept {
		return m_StreamDecoder.Empty();
	}

	std::size_t PacketFrameDecoder::GetMaximumPacketSize() const noexcept {
		return m_StreamDecoder.GetMaximumPacketSize();
	}
}
