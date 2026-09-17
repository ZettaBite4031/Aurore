#include <Aurore/Protocol/PacketCodec.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
#pragma region Protocol Session Tests

	namespace {

		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PacketFrameDecoder;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolSession;
		using Aurore::Protocol::ProtocolState;
		using Aurore::Protocol::ServerStatus;
		using Aurore::Util::ByteBuffer;
		using Aurore::Util::ByteReader;

		PacketFrame MakeHandshakeFrame(
			std::int32_t protocolVersion,
			std::string_view serverAddress,
			std::uint16_t serverPort,
			std::int32_t nextState,
			std::span<const std::byte> trailingData = {}) {

			ByteBuffer payload;
			payload.WriteVarInt(protocolVersion);
			payload.WriteString(serverAddress);
			payload.WriteUInt16(serverPort);
			payload.WriteVarInt(nextState);
			payload.WriteBytes(trailingData);

			return PacketFrame{
				.PacketId = 0x00,
				.Payload = std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end())
			};
		}

		PacketFrame MakeHandshakeFrame(
			std::int32_t protocolVersion = 774,
			std::string_view serverAddress = "localhost",
			std::uint16_t serverPort = 25565) {

			ByteBuffer payload;
			payload.WriteVarInt(protocolVersion);
			payload.WriteString(serverAddress);
			payload.WriteUInt16(serverPort);
			payload.WriteVarInt(static_cast<std::int32_t>(HandshakeIntention::Status));

			return PacketFrame{
				.PacketId = 0x00,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end())
			};
		}

		PacketFrame MakeStatusRequestFrame(std::span<const std::byte> payload = {}) {
			return PacketFrame{
				.PacketId = 0x00,
				.Payload = std::vector<std::byte>(payload.begin(), payload.end())
			};
		}

		PacketFrame MakePingRequestFrame(
			std::int64_t value,
			std::span<const std::byte> trailingData = {}) {

			ByteBuffer payload;
			payload.WriteInt64(value);
			payload.WriteBytes(trailingData);

			return PacketFrame{
				.PacketId = 0x01,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end())
			};
		}

		void EnterStatusState(ProtocolSession& session) {
			const auto result = session.HandlePacket(MakeHandshakeFrame());

			ASSERT_TRUE(result.has_value());
			ASSERT_EQ(session.GetState(), ProtocolState::Status);
		}

		void SendStatusRequest(ProtocolSession& session) {
			const auto result = session.HandlePacket(MakeStatusRequestFrame());

			ASSERT_TRUE(result.has_value());
			ASSERT_EQ(result->OutboundPackets.size(), 1);
			ASSERT_FALSE(result->Disconnect);
		}

		std::optional<PacketFrame> DecodeSingleFrame(const Aurore::Protocol::ClientboundPacket& packet) {
			return Aurore::Protocol::EncodeClientboundPacket(packet);
		}
	}

	TEST(ProtocolSessionTests, AcceptsStatusHandshake) {
		const auto frame = MakeHandshakeFrame(
			774,
			"localhost",
			25565,
			static_cast<std::int32_t>(HandshakeIntention::Status));

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(result->OutboundPackets.empty());
		EXPECT_FALSE(result->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
		EXPECT_TRUE(session.HasHandshake());

		const auto* handshake = session.GetHandshake();

		ASSERT_NE(handshake, nullptr);
		EXPECT_EQ(handshake->ProtocolVersion, 774);
		EXPECT_EQ(handshake->ServerAddress, "localhost");
		EXPECT_EQ(handshake->ServerPort, 25565);
		EXPECT_EQ(handshake->Intention, HandshakeIntention::Status);
	}

	TEST(ProtocolSessionTests, AcceptsLoginHandshake) {
		const auto frame = MakeHandshakeFrame(
			774,
			"example.org",
			25566,
			static_cast<std::int32_t>(HandshakeIntention::Login));

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(session.GetState(), ProtocolState::Login);
		EXPECT_TRUE(session.HasHandshake());

		const auto* handshake = session.GetHandshake();

		ASSERT_NE(handshake, nullptr);
		EXPECT_EQ(handshake->ProtocolVersion, 774);
		EXPECT_EQ(handshake->ServerAddress, "example.org");
		EXPECT_EQ(handshake->ServerPort, 25566);
		EXPECT_EQ(handshake->Intention, HandshakeIntention::Login);
	}

	TEST(ProtocolSessionTests, RejectsUnexpectedHandshakePacketId) {
		PacketFrame frame{
			.PacketId = 0x01
		};

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionTests, RejectsMalformedHandshakeWithoutChangingState) {
		ByteBuffer payload;
		payload.WriteVarInt(774);

		PacketFrame frame{
			.PacketId = 0x00,
			.Payload = std::vector<std::byte>(
				payload.Bytes().begin(),
				payload.Bytes().end())
		};

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionTests, RejectsInvalidNextStateWithoutChangingState) {
		const auto frame = MakeHandshakeFrame(
			774,
			"localhost",
			25565,
			3);

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::InvalidNextState);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionTests, RejectsTrailingHandshakeDataWithoutChangingState) {
		const std::byte trailingByte{ 0xFF };

		const auto frame = MakeHandshakeFrame(
			774,
			"localhost",
			25565,
			static_cast<std::int32_t>(HandshakeIntention::Status),
			std::span<const std::byte>(&trailingByte, 1));

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionTests, RejectsServerAddressOverMaximumLength) {
		const std::string serverAddress(256 + 1, 'a');

		const auto frame = MakeHandshakeFrame(
			774,
			serverAddress,
			25565,
			static_cast<std::int32_t>(HandshakeIntention::Status));

		ProtocolSession session;

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Handshake);
		EXPECT_FALSE(session.HasHandshake());
	}

	TEST(ProtocolSessionTests, ExplicitDisconnectChangesState) {
		ProtocolSession session;

		session.Disconnect();

		EXPECT_EQ(session.GetState(), ProtocolState::Disconnected);
		EXPECT_TRUE(session.IsDisconnected());
	}

	TEST(ProtocolSessionTests, DisconnectedSessionRejectsPackets) {
		ProtocolSession session;
		session.Disconnect();

		const PacketFrame frame{
			.PacketId = 0x00
		};

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Disconnected);
	}


	TEST(ProtocolStatusTests, StatusRequestProducesFramedJsonResponse) {
		ServerStatus status{
			.VersionName = "Aurore Test",
			.ProtocolVersion = 774,
			.MaximumPlayers = 100,
			.OnlinePlayers = 7,
			.Description = "Aurore Development Server"
		};

		ProtocolSession session(status);
		EnterStatusState(session);

		const auto result = session.HandlePacket(MakeStatusRequestFrame());

		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->OutboundPackets.size(), 1);
		EXPECT_FALSE(result->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);

		const auto frame = DecodeSingleFrame(result->OutboundPackets.front());

		ASSERT_TRUE(frame.has_value());
		EXPECT_EQ(frame->PacketId, 0x00);

		ByteReader reader(frame->Payload);

		const auto json = reader.ReadString(frame->Payload.size());

		ASSERT_TRUE(json.has_value());
		EXPECT_TRUE(reader.Empty());
		EXPECT_EQ(
			*json,
			R"({"version":{"name":"Aurore Test","protocol":774},"players":{"max":100,"online":7},"description":{"text":"Aurore Development Server"}})"
		);
	}

	TEST(ProtocolStatusTests, StatusResponseEscapesJsonStrings) {
		ServerStatus status{
			.VersionName = "Aurore \"Test\"",
			.ProtocolVersion = 774,
			.MaximumPlayers = 20,
			.OnlinePlayers = 0,
			.Description = "Line 1\nLine 2\\Ready\t"
		};

		ProtocolSession session(status);
		EnterStatusState(session);

		const auto result = session.HandlePacket(MakeStatusRequestFrame());

		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->OutboundPackets.size(), 1);

		const auto frame = DecodeSingleFrame(result->OutboundPackets.front());

		ASSERT_TRUE(frame.has_value());

		ByteReader reader(frame->Payload);

		const auto json = reader.ReadString(frame->Payload.size());

		ASSERT_TRUE(json.has_value());
		EXPECT_EQ(
			*json,
			R"({"version":{"name":"Aurore \"Test\"","protocol":774},"players":{"max":20,"online":0},"description":{"text":"Line 1\nLine 2\\Ready\t"}})"
		);
	}

	TEST(ProtocolStatusTests, RejectsStatusRequestWithPayload) {
		const std::byte unexpectedData{ 0x01 };

		ProtocolSession session;
		EnterStatusState(session);

		const auto result = session.HandlePacket(
			MakeStatusRequestFrame(std::span<const std::byte>(&unexpectedData, 1)));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);

		const auto validResult = session.HandlePacket(MakeStatusRequestFrame());

		EXPECT_TRUE(validResult.has_value());
	}

	TEST(ProtocolStatusTests, RejectsSecondStatusRequest) {
		ProtocolSession session;
		EnterStatusState(session);
		SendStatusRequest(session);

		const auto result = session.HandlePacket(MakeStatusRequestFrame());

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
	}

	TEST(ProtocolPingTests, RejectsPingBeforeStatusRequest) {
		ProtocolSession session;
		EnterStatusState(session);

		const auto result = session.HandlePacket(MakePingRequestFrame(12345));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
		EXPECT_FALSE(session.IsDisconnected());
	}

	TEST(ProtocolPingTests, EchoesExactSignedPayloadAndRequestsCloseAfterFlush) {
		const std::array<std::int64_t, 5> values{
			std::numeric_limits<std::int64_t>::min(),
			-1,
			0,
			123456789012345LL,
			std::numeric_limits<std::int64_t>::max()
		};

		for (const auto value : values) {
			SCOPED_TRACE(::testing::Message() << "Ping value: " << value);

			ProtocolSession session;
			EnterStatusState(session);
			SendStatusRequest(session);

			const auto result = session.HandlePacket(MakePingRequestFrame(value));

			ASSERT_TRUE(result.has_value());
			ASSERT_EQ(result->OutboundPackets.size(), 1);
			EXPECT_TRUE(result->Disconnect);
			EXPECT_TRUE(session.IsDisconnected());
			EXPECT_EQ(session.GetState(), ProtocolState::Disconnected);

			const auto frame = DecodeSingleFrame(result->OutboundPackets.front());

			ASSERT_TRUE(frame.has_value());
			EXPECT_EQ(frame->PacketId, 0x01);

			ByteReader reader(frame->Payload);

			const auto echoedValue = reader.ReadInt64();

			ASSERT_TRUE(echoedValue.has_value());
			EXPECT_EQ(*echoedValue, value);
			EXPECT_TRUE(reader.Empty());
		}
	}

	TEST(ProtocolPingTests, RejectsTruncatedPingWithoutDisconnecting) {
		const std::array<std::byte, 4> payload{
			std::byte{ 0x01 },
			std::byte{ 0x02 },
			std::byte{ 0x03 },
			std::byte{ 0x04 }
		};

		ProtocolSession session;
		EnterStatusState(session);
		SendStatusRequest(session);

		const PacketFrame frame{
			.PacketId = 0x01,
			.Payload = std::vector<std::byte>(payload.begin(), payload.end())
		};

		const auto result = session.HandlePacket(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
		EXPECT_FALSE(session.IsDisconnected());

		const auto validResult = session.HandlePacket(MakePingRequestFrame(42));

		EXPECT_TRUE(validResult.has_value());
	}

	TEST(ProtocolPingTests, RejectsTrailingPingDataWithoutDisconnecting) {
		const std::byte trailingData{ 0xFF };

		ProtocolSession session;
		EnterStatusState(session);
		SendStatusRequest(session);

		const auto result = session.HandlePacket(
			MakePingRequestFrame(
				42,
				std::span<const std::byte>(&trailingData, 1)));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
		EXPECT_EQ(session.GetState(), ProtocolState::Status);
		EXPECT_FALSE(session.IsDisconnected());
	}

	TEST(ProtocolPingTests, RejectsPacketsAfterSuccessfulPing) {
		ProtocolSession session;
		EnterStatusState(session);
		SendStatusRequest(session);

		const auto pingResult = session.HandlePacket(MakePingRequestFrame(42));

		ASSERT_TRUE(pingResult.has_value());
		ASSERT_TRUE(session.IsDisconnected());

		const auto laterResult = session.HandlePacket(MakePingRequestFrame(43));

		ASSERT_FALSE(laterResult.has_value());
		EXPECT_EQ(laterResult.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Disconnected);
	}

#pragma endregion
}
