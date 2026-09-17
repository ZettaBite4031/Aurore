#include <Aurore/Protocol/PacketCodec.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Handshake.hpp>
#include <Aurore/Protocol/Packets/Status.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Protocol::ClientboundPacket;
		using Aurore::Protocol::EncodeClientboundPacket;
		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolSession;
		using Aurore::Protocol::ProtocolState;
		using Aurore::Protocol::ServerStatus;
		using Aurore::Util::ByteBuffer;

		namespace Handshake = Aurore::Protocol::Packets::Handshake;
		namespace Status = Aurore::Protocol::Packets::Status;

		std::vector<std::byte> MakeBytes(std::initializer_list<std::uint8_t> values) {
			std::vector<std::byte> result;
			result.reserve(values.size());

			for (const auto value : values)
				result.push_back(static_cast<std::byte>(value));

			return result;
		}

		PacketFrame MakeFrame(std::int32_t packet_id, const ByteBuffer& payload) {
			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end()),
			};
		}

		PacketFrame MakeHandshakeFrame(
			std::string_view address,
			std::int32_t intention,
			std::int32_t protocol_version = 774,
			std::uint16_t port = 25565) {

			ByteBuffer payload;
			payload.WriteVarInt(protocol_version);
			payload.WriteString(address);
			payload.WriteUInt16(port);
			payload.WriteVarInt(intention);

			return MakeFrame(
				Handshake::Serverbound::HandshakePacketId,
				payload);
		}

		void ExpectBytesEqual(
			std::span<const std::byte> actual,
			std::span<const std::byte> expected) {

			ASSERT_EQ(actual.size(), expected.size());
			EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
		}
	}

	TEST(HandshakeCodecTests, DecodesStatusIntention) {
		const auto frame = MakeHandshakeFrame(
			"localhost",
			static_cast<std::int32_t>(Handshake::Intention::Status));

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Handshake::Serverbound::Handshake>(*result));

		const auto& packet =
			std::get<Handshake::Serverbound::Handshake>(*result);

		EXPECT_EQ(packet.ProtocolVersion, 774);
		EXPECT_EQ(packet.ServerAddress, "localhost");
		EXPECT_EQ(packet.ServerPort, 25565);
		EXPECT_EQ(packet.Intention, Handshake::Intention::Status);
	}

	TEST(HandshakeCodecTests, DecodesLoginIntention) {
		const auto frame = MakeHandshakeFrame(
			"example.test",
			static_cast<std::int32_t>(Handshake::Intention::Login),
			123,
			24444);

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_TRUE(result.has_value());

		const auto& packet =
			std::get<Handshake::Serverbound::Handshake>(*result);

		EXPECT_EQ(packet.ProtocolVersion, 123);
		EXPECT_EQ(packet.ServerAddress, "example.test");
		EXPECT_EQ(packet.ServerPort, 24444);
		EXPECT_EQ(packet.Intention, Handshake::Intention::Login);
	}

	TEST(HandshakeCodecTests, AcceptsMaximumServerAddressLength) {
		const std::string address(
			Handshake::Serverbound::MaximumServerAddressBytes,
			'a');

		const auto frame = MakeHandshakeFrame(
			address,
			static_cast<std::int32_t>(Handshake::Intention::Status));

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_TRUE(result.has_value());

		const auto& packet =
			std::get<Handshake::Serverbound::Handshake>(*result);

		EXPECT_EQ(packet.ServerAddress, address);
	}

	TEST(HandshakeCodecTests, RejectsOversizedServerAddress) {
		const std::string address(
			Handshake::Serverbound::MaximumServerAddressBytes + 1,
			'a');

		const auto frame = MakeHandshakeFrame(
			address,
			static_cast<std::int32_t>(Handshake::Intention::Status));

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
	}

	TEST(HandshakeCodecTests, RejectsTruncatedPacket) {
		ByteBuffer payload;
		payload.WriteVarInt(774);

		const auto result = Handshake::Serverbound::Decode(
			MakeFrame(
				Handshake::Serverbound::HandshakePacketId,
				payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
	}

	TEST(HandshakeCodecTests, RejectsInvalidNextState) {
		const auto frame = MakeHandshakeFrame(
			"localhost",
			3);

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::InvalidNextState);
	}

	TEST(HandshakeCodecTests, RejectsTrailingData) {
		ByteBuffer payload;
		payload.WriteVarInt(774);
		payload.WriteString("localhost");
		payload.WriteUInt16(25565);
		payload.WriteVarInt(
			static_cast<std::int32_t>(
				Handshake::Intention::Status));

		payload.WriteUnsignedByte(0xAA);

		const auto result = Handshake::Serverbound::Decode(
			MakeFrame(
				Handshake::Serverbound::HandshakePacketId,
				payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
	}

	TEST(HandshakeCodecTests, RejectsWrongPacketId) {
		const PacketFrame frame{
			.PacketId = 0x01,
			.Payload = {},
		};

		const auto result = Handshake::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
	}

	TEST(StatusCodecTests, DecodesRequest) {
		const PacketFrame frame{
			.PacketId = Status::Serverbound::RequestPacketId,
			.Payload = {},
		};

		const auto result = Status::Serverbound::Decode(frame);

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(
			std::holds_alternative<
			Status::Serverbound::Request>(*result));
	}

	TEST(StatusCodecTests, RequestRejectsTrailingData) {
		const PacketFrame frame{
			.PacketId = Status::Serverbound::RequestPacketId,
			.Payload = MakeBytes({ 0x00 }),
		};

		const auto result = Status::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
	}

	TEST(StatusCodecTests, DecodesPingPayload) {
		constexpr std::int64_t Payload{
			std::numeric_limits<std::int64_t>::min()
		};

		ByteBuffer payload;
		payload.WriteInt64(Payload);

		const auto result = Status::Serverbound::Decode(
			MakeFrame(
				Status::Serverbound::PingPacketId,
				payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(
			std::holds_alternative<
			Status::Serverbound::Ping>(*result));

		EXPECT_EQ(
			std::get<Status::Serverbound::Ping>(*result).Payload,
			Payload);
	}

	TEST(StatusCodecTests, PingRejectsTruncatedPayload) {
		const PacketFrame frame{
			.PacketId = Status::Serverbound::PingPacketId,
			.Payload = MakeBytes({
				0x00,
				0x00,
				0x00,
				0x00,
			}),
		};

		const auto result = Status::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
	}

	TEST(StatusCodecTests, PingRejectsTrailingData) {
		ByteBuffer payload;
		payload.WriteInt64(42);
		payload.WriteUnsignedByte(0xAA);

		const auto result = Status::Serverbound::Decode(
			MakeFrame(
				Status::Serverbound::PingPacketId,
				payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
	}

	TEST(StatusCodecTests, RejectsUnknownPacketId) {
		const PacketFrame frame{
			.PacketId = 0x7F,
			.Payload = {},
		};

		const auto result = Status::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
	}

	TEST(StatusCodecTests, EncodesExactResponsePayload) {
		const ServerStatus status{
			.VersionName = "Aurore Test",
			.ProtocolVersion = 774,
			.MaximumPlayers = 32,
			.OnlinePlayers = 7,
			.Description = "Codec Test",
		};

		const auto frame = Status::Clientbound::Encode(
			Status::Clientbound::Response{
				.Status = status,
			});

		ASSERT_EQ(
			frame.PacketId,
			Status::Clientbound::ResponsePacketId);

		ByteBuffer expected_payload;

		expected_payload.WriteString(
			R"({"version":{"name":"Aurore Test","protocol":774},"players":{"max":32,"online":7},"description":{"text":"Codec Test"}})");

		ExpectBytesEqual(
			frame.Payload,
			expected_payload.Bytes());
	}

	TEST(StatusCodecTests, EscapesStatusJsonStrings) {
		const ServerStatus status{
			.VersionName = "Aurore\"Test",
			.ProtocolVersion = 774,
			.MaximumPlayers = 20,
			.OnlinePlayers = 0,
			.Description = "Line 1\nLine 2\\End",
		};

		const auto frame = Status::Clientbound::Encode(
			Status::Clientbound::Response{
				.Status = status,
			});

		ByteBuffer expected_payload;

		expected_payload.WriteString(
			R"({"version":{"name":"Aurore\"Test","protocol":774},"players":{"max":20,"online":0},"description":{"text":"Line 1\nLine 2\\End"}})");

		ExpectBytesEqual(
			frame.Payload,
			expected_payload.Bytes());
	}

	TEST(StatusCodecTests, EncodesExactPongFrame) {
		const auto packet_frame = Status::Clientbound::Encode(
			Status::Clientbound::Pong{
				.Payload = 42,
			});

		const auto encoded = EncodePacketFrame(
			packet_frame.PacketId,
			packet_frame.Payload);

		const auto expected = MakeBytes({
			0x09,
			0x01,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x2A,
			});

		ExpectBytesEqual(
			encoded.Bytes(),
			expected);
	}

	TEST(PacketCodecTests, DispatchesGlobalClientboundVariant) {
		const ClientboundPacket packet{
			Status::Clientbound::Pong{
				.Payload = 1234,
			}
		};

		const auto frame = EncodeClientboundPacket(packet);

		EXPECT_EQ(
			frame.PacketId,
			Status::Clientbound::PongPacketId);

		Aurore::Util::ByteReader reader(frame.Payload);

		const auto payload = reader.ReadInt64();

		ASSERT_TRUE(payload.has_value());
		EXPECT_EQ(*payload, 1234);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ProtocolSessionCodecIntegrationTests, FailedHandshakeDoesNotCommitState) {
		ProtocolSession session;

		const auto result = session.HandlePacket(
			MakeHandshakeFrame(
				"localhost",
				3));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::InvalidNextState);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionCodecIntegrationTests, StatusRequestProducesTypedResponse) {
		ProtocolSession session;

		const auto handshake_result = session.HandlePacket(
			MakeHandshakeFrame(
				"localhost",
				static_cast<std::int32_t>(
					Handshake::Intention::Status)));

		ASSERT_TRUE(handshake_result.has_value());
		ASSERT_EQ(session.GetState(), ProtocolState::Status);

		const PacketFrame request{
			.PacketId = Status::Serverbound::RequestPacketId,
			.Payload = {},
		};

		const auto request_result =
			session.HandlePacket(request);

		ASSERT_TRUE(request_result.has_value());
		ASSERT_EQ(
			request_result->OutboundPackets.size(),
			1);

		EXPECT_TRUE(
			std::holds_alternative<
			Status::Clientbound::Response>(
				request_result->OutboundPackets.front()));

		EXPECT_FALSE(request_result->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
	}
}
