#include <Aurore/Protocol/PacketCodec.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/ProtocolConnection.hpp>
#include <Aurore/Protocol/ProtocolPipeline.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {

namespace Helpers {

using Aurore::Protocol::ClientboundPacket;
using Aurore::Protocol::ConnectionDisposition;
using Aurore::Protocol::EncodeClientboundPacket;
using Aurore::Protocol::EncodePacketFrame;
using Aurore::Protocol::FrameError;
using Aurore::Protocol::HandshakeIntention;
using Aurore::Protocol::PacketFrame;
using Aurore::Protocol::PacketFrameDecoder;
using Aurore::Protocol::PreparedPacketFrame;
using Aurore::Protocol::ProtocolConnection;
using Aurore::Protocol::ProtocolError;
using Aurore::Protocol::ProtocolPipeline;
using Aurore::Protocol::ProtocolPipelineConfiguration;
using Aurore::Protocol::ProtocolState;
using Aurore::Protocol::ProtocolTransformError;
using Aurore::Protocol::ServerStatus;
using Aurore::Util::ByteBuffer;
using Aurore::Util::ByteReader;

std::vector<std::byte> MakeBytes(std::initializer_list<std::uint8_t> values) {
	std::vector<std::byte> result;
	result.reserve(values.size());

	for (const auto value : values) {
		result.push_back(static_cast<std::byte>(value));
	}

	return result;
}

void AppendBytes(std::vector<std::byte>& destination, std::span<const std::byte> source) {
	destination.insert(destination.end(), source.begin(), source.end());
}

std::span<const std::byte> GetPayloadBytes(const PacketFrame& frame) noexcept {
	return std::span<const std::byte>(frame.Payload.data(), frame.Payload.size());
}

ByteBuffer MakeEncodedHandshakeFrame(
	std::int32_t protocol_version,
	HandshakeIntention intention) {

	ByteBuffer payload;
	payload.WriteVarInt(protocol_version);
	payload.WriteString("localhost");
	payload.WriteUInt16(25565);
	payload.WriteVarInt(static_cast<std::int32_t>(intention));

	return EncodePacketFrame(0x00, payload.Bytes());
}

ByteBuffer MakeEncodedHandshakeFrame() {
	return MakeEncodedHandshakeFrame(774, HandshakeIntention::Status);
}

ByteBuffer MakeEncodedLoginStartFrame(std::string_view username = "PlayerOne") {
	ByteBuffer payload;
	payload.WriteString(username);
	payload.WriteUuid({});

	return EncodePacketFrame(
		Aurore::Protocol::Packets::Login::Serverbound::StartPacketId,
		payload.Bytes());
}

ByteBuffer MakeEncodedStatusRequestFrame() {
	return EncodePacketFrame(0x00, std::span<const std::byte>{});
}

ByteBuffer MakeEncodedPingRequestFrame(std::int64_t value) {
	ByteBuffer payload;
	payload.WriteInt64(value);

	return EncodePacketFrame(0x01, payload.Bytes());
}

ByteBuffer MakeMalformedPingFrame() {
	const auto payload = MakeBytes({ 0x01, 0x02, 0x03, 0x04 });
	return EncodePacketFrame(0x01, payload);
}

std::optional<PacketFrame> DecodeSingleFrame(const ByteBuffer& encoded) {
	PacketFrameDecoder decoder;
	decoder.Append(encoded.Bytes());

	auto result = decoder.TryDecode();

	if (!result.has_value() || !result->has_value() || !decoder.Empty()) {
		return std::nullopt;
	}

	return std::move(result->value());
}

} // namespace Helpers

using namespace Helpers;

TEST(PacketCodecTests, PreparedFrameTransfersPayloadStorage) {
	ClientboundPacket packet = PreparedPacketFrame{
		.Frame = PacketFrame{
			.PacketId = 0x07,
			.Payload = std::vector<std::byte>(256 * 1024, std::byte{ 0x42 }),
		},
	};

	auto* original_data = std::get<PreparedPacketFrame>(packet).Frame.Payload.data();

	auto encoded = EncodeClientboundPacket(std::move(packet));

	EXPECT_EQ(encoded.PacketId, 0x07);
	EXPECT_EQ(encoded.Payload.size(), 256u * 1024u);
	EXPECT_EQ(encoded.Payload.data(), original_data);
}

TEST(ProtocolPipelineTests, RejectsZeroOutboundActionLimit) {
	EXPECT_THROW(
		ProtocolPipeline(ProtocolPipelineConfiguration{
			.MaximumFramedPacketSize = 1024,
			.MaximumDecompressedPacketSize = 1024,
			.MaximumOutboundActionBytes = 0,
		}),
		std::invalid_argument);
}

TEST(ProtocolConnectionTests, StartsOpenWithNoBufferedData) {
	ProtocolConnection connection;

	EXPECT_TRUE(connection.IsOpen());
	EXPECT_FALSE(connection.IsClosing());
	EXPECT_EQ(connection.GetDisposition(), ConnectionDisposition::KeepOpen);
	EXPECT_EQ(connection.BufferedBytes(), 0);
	EXPECT_EQ(connection.GetSession().GetState(), ProtocolState::Handshake);
}

TEST(ProtocolConnectionTests, HandlesHandshakeFragmentedOneByteAtATime) {
	const auto handshake = MakeEncodedHandshakeFrame();

	ProtocolConnection connection;

	for (std::size_t index = 0; index < handshake.Size(); ++index) {
		const auto result = connection.Receive(handshake.Bytes().subspan(index, 1));

		EXPECT_FALSE(result.HasError());
		EXPECT_TRUE(result.OutboundFrames.empty());
		EXPECT_EQ(result.Disposition, ConnectionDisposition::KeepOpen);
	}

	EXPECT_TRUE(connection.IsOpen());
	EXPECT_EQ(connection.BufferedBytes(), 0);
	EXPECT_EQ(connection.GetSession().GetState(), ProtocolState::Status);
	EXPECT_TRUE(connection.GetSession().HasHandshake());
}

TEST(ProtocolConnectionTests, ProcessesCompleteStatusSequenceInSingleReceive) {
	constexpr std::int64_t PingValue = 123456789012345LL;

	const auto handshake = MakeEncodedHandshakeFrame();
	const auto statusRequest = MakeEncodedStatusRequestFrame();
	const auto pingRequest = MakeEncodedPingRequestFrame(PingValue);

	std::vector<std::byte> received;
	AppendBytes(received, handshake.Bytes());
	AppendBytes(received, statusRequest.Bytes());
	AppendBytes(received, pingRequest.Bytes());

	ProtocolConnection connection;

	const auto result = connection.Receive(received);

	ASSERT_FALSE(result.HasError());
	ASSERT_EQ(result.OutboundFrames.size(), 2);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseAfterFlush);
	EXPECT_EQ(connection.GetDisposition(), ConnectionDisposition::CloseAfterFlush);
	EXPECT_TRUE(connection.IsClosing());
	EXPECT_FALSE(connection.IsOpen());
	EXPECT_TRUE(connection.GetSession().IsDisconnected());

	const auto statusResponse = DecodeSingleFrame(result.OutboundFrames[0]);
	const auto pingResponse = DecodeSingleFrame(result.OutboundFrames[1]);

	ASSERT_TRUE(statusResponse.has_value());
	ASSERT_TRUE(pingResponse.has_value());

	EXPECT_EQ(statusResponse->PacketId, 0x00);
	EXPECT_EQ(pingResponse->PacketId, 0x01);

	ByteReader pingReader(GetPayloadBytes(*pingResponse));

	const auto echoedValue = pingReader.ReadInt64();

	ASSERT_TRUE(echoedValue.has_value());
	EXPECT_EQ(*echoedValue, PingValue);
	EXPECT_TRUE(pingReader.Empty());
}

TEST(ProtocolConnectionTests, PreservesIncompleteTrailingFrameBetweenReceives) {
	const auto handshake = MakeEncodedHandshakeFrame();
	const auto statusRequest = MakeEncodedStatusRequestFrame();

	ASSERT_GT(statusRequest.Size(), 1);

	std::vector<std::byte> firstReceive;
	AppendBytes(firstReceive, handshake.Bytes());
	AppendBytes(firstReceive, statusRequest.Bytes().first(1));

	ProtocolConnection connection;

	const auto firstResult = connection.Receive(firstReceive);

	ASSERT_FALSE(firstResult.HasError());
	EXPECT_TRUE(firstResult.OutboundFrames.empty());
	EXPECT_EQ(firstResult.Disposition, ConnectionDisposition::KeepOpen);
	EXPECT_EQ(connection.GetSession().GetState(), ProtocolState::Status);
	EXPECT_EQ(connection.BufferedBytes(), 1);

	const auto secondResult = connection.Receive(statusRequest.Bytes().subspan(1));

	ASSERT_FALSE(secondResult.HasError());
	ASSERT_EQ(secondResult.OutboundFrames.size(), 1);
	EXPECT_EQ(secondResult.Disposition, ConnectionDisposition::KeepOpen);
	EXPECT_EQ(connection.BufferedBytes(), 0);

	const auto response = DecodeSingleFrame(secondResult.OutboundFrames.front());

	ASSERT_TRUE(response.has_value());
	EXPECT_EQ(response->PacketId, 0x00);
}

TEST(ProtocolConnectionTests, MalformedFrameClosesImmediately) {
	const auto malformedLength = MakeBytes({
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0x0F
	});

	ProtocolConnection connection;

	const auto result = connection.Receive(malformedLength);

	ASSERT_TRUE(result.HasError());
	ASSERT_TRUE(std::holds_alternative<FrameError>(*result.Error));

	EXPECT_EQ(std::get<FrameError>(*result.Error), FrameError::NegativeLength);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseImmediately);
	EXPECT_TRUE(result.OutboundFrames.empty());
	EXPECT_TRUE(connection.IsClosing());
	EXPECT_TRUE(connection.GetSession().IsDisconnected());
	EXPECT_EQ(connection.BufferedBytes(), 0);
}

TEST(ProtocolConnectionTests, ProtocolErrorClosesImmediately) {
	const auto unexpectedPacket = EncodePacketFrame(
		0x01,
		std::span<const std::byte>{});

	ProtocolConnection connection;

	const auto result = connection.Receive(unexpectedPacket.Bytes());

	ASSERT_TRUE(result.HasError());
	ASSERT_TRUE(std::holds_alternative<ProtocolError>(*result.Error));

	EXPECT_EQ(std::get<ProtocolError>(*result.Error), ProtocolError::UnexpectedPacket);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseImmediately);
	EXPECT_TRUE(result.OutboundFrames.empty());
	EXPECT_TRUE(connection.GetSession().IsDisconnected());
}

TEST(ProtocolConnectionTests, PreservesGeneratedResponseBeforeLaterProtocolError) {
	const auto handshake = MakeEncodedHandshakeFrame();
	const auto statusRequest = MakeEncodedStatusRequestFrame();
	const auto malformedPing = MakeMalformedPingFrame();

	std::vector<std::byte> received;
	AppendBytes(received, handshake.Bytes());
	AppendBytes(received, statusRequest.Bytes());
	AppendBytes(received, malformedPing.Bytes());

	ProtocolConnection connection;

	const auto result = connection.Receive(received);

	ASSERT_TRUE(result.HasError());
	ASSERT_TRUE(std::holds_alternative<ProtocolError>(*result.Error));
	ASSERT_EQ(result.OutboundFrames.size(), 1);

	EXPECT_EQ(std::get<ProtocolError>(*result.Error), ProtocolError::MalformedPacket);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseAfterFlush);
	EXPECT_EQ(connection.GetDisposition(), ConnectionDisposition::CloseAfterFlush);
	EXPECT_TRUE(connection.GetSession().IsDisconnected());

	const auto response = DecodeSingleFrame(result.OutboundFrames.front());

	ASSERT_TRUE(response.has_value());
	EXPECT_EQ(response->PacketId, 0x00);
}

TEST(ProtocolConnectionTests, IgnoresAdditionalInputAfterCloseRequested) {
	const auto handshake = MakeEncodedHandshakeFrame();
	const auto statusRequest = MakeEncodedStatusRequestFrame();
	const auto pingRequest = MakeEncodedPingRequestFrame(42);

	std::vector<std::byte> received;
	AppendBytes(received, handshake.Bytes());
	AppendBytes(received, statusRequest.Bytes());
	AppendBytes(received, pingRequest.Bytes());

	ProtocolConnection connection;

	const auto initialResult = connection.Receive(received);

	ASSERT_EQ(initialResult.Disposition, ConnectionDisposition::CloseAfterFlush);
	ASSERT_TRUE(connection.IsClosing());

	const auto laterResult = connection.Receive(handshake.Bytes());

	EXPECT_FALSE(laterResult.HasError());
	EXPECT_TRUE(laterResult.OutboundFrames.empty());
	EXPECT_EQ(laterResult.Disposition, ConnectionDisposition::CloseAfterFlush);
	EXPECT_EQ(connection.GetSession().GetState(), ProtocolState::Disconnected);
	EXPECT_EQ(connection.BufferedBytes(), 0);
}

TEST(ProtocolConnectionTests, MismatchedStatusProtocolCanQueryStatus) {
	const auto handshake = MakeEncodedHandshakeFrame(773, HandshakeIntention::Status);

	ProtocolConnection connection;

	const auto handshakeResult = connection.Receive(handshake.Bytes());

	ASSERT_FALSE(handshakeResult.HasError());
	EXPECT_TRUE(handshakeResult.OutboundFrames.empty());
	EXPECT_EQ(connection.GetSession().GetState(), ProtocolState::Status);

	const auto* handshakeData = connection.GetSession().GetHandshake();
	ASSERT_NE(handshakeData, nullptr);
	EXPECT_EQ(handshakeData->ProtocolVersion, 773);

	const auto statusResult = connection.Receive(MakeEncodedStatusRequestFrame().Bytes());

	ASSERT_FALSE(statusResult.HasError());
	ASSERT_EQ(statusResult.OutboundFrames.size(), 1u);
	EXPECT_EQ(statusResult.Disposition, ConnectionDisposition::KeepOpen);
}

TEST(ProtocolConnectionTests, MismatchedLoginProtocolReceivesDisconnect) {
	const auto handshake = MakeEncodedHandshakeFrame(773, HandshakeIntention::Login);

	ProtocolConnection connection;

	const auto result = connection.Receive(handshake.Bytes());

	ASSERT_FALSE(result.HasError());
	EXPECT_TRUE(result.Requests.empty());
	ASSERT_EQ(result.OutboundFrames.size(), 1u);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseAfterFlush);
	EXPECT_TRUE(connection.GetSession().IsDisconnected());
	EXPECT_EQ(connection.BufferedBytes(), 0u);

	const auto disconnect = DecodeSingleFrame(result.OutboundFrames.front());
	ASSERT_TRUE(disconnect.has_value());
	EXPECT_EQ(
		disconnect->PacketId,
		Aurore::Protocol::Packets::Login::Clientbound::DisconnectPacketId);
}

TEST(ProtocolConnectionTests, CoalescedMismatchedLoginInputProducesNoCoreRequest) {
	const auto handshake = MakeEncodedHandshakeFrame(773, HandshakeIntention::Login);
	const auto loginStart = MakeEncodedLoginStartFrame();

	std::vector<std::byte> received;
	AppendBytes(received, handshake.Bytes());
	AppendBytes(received, loginStart.Bytes());

	ProtocolConnection connection;

	const auto result = connection.Receive(received);

	ASSERT_FALSE(result.HasError());
	EXPECT_TRUE(result.Requests.empty());
	ASSERT_EQ(result.OutboundFrames.size(), 1u);
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseAfterFlush);
	EXPECT_TRUE(connection.GetSession().IsDisconnected());
	EXPECT_EQ(connection.BufferedBytes(), 0u);
}

TEST(ProtocolConnectionTests, RejectsOutboundActionLargerThanConfiguredLimit) {
	ServerStatus status;
	status.Description = std::string(1024, 'x');

	ProtocolConnection connection(
		std::move(status),
		ProtocolPipelineConfiguration{
			.MaximumFramedPacketSize = 4096,
			.MaximumDecompressedPacketSize = 4096,
			.MaximumOutboundActionBytes = 128,
		});

	const auto handshake = MakeEncodedHandshakeFrame();
	const auto handshakeResult = connection.Receive(handshake.Bytes());

	ASSERT_FALSE(handshakeResult.HasError());

	const auto result = connection.Receive(MakeEncodedStatusRequestFrame().Bytes());

	ASSERT_TRUE(result.HasError());
	ASSERT_TRUE(std::holds_alternative<ProtocolTransformError>(*result.Error));
	EXPECT_EQ(
		std::get<ProtocolTransformError>(*result.Error),
		ProtocolTransformError::OutboundActionTooLarge);
	EXPECT_TRUE(result.OutboundFrames.empty());
	EXPECT_TRUE(result.Requests.empty());
	EXPECT_EQ(result.Disposition, ConnectionDisposition::CloseImmediately);
	EXPECT_TRUE(connection.GetSession().IsDisconnected());
}

} // namespace Aurore::Tests

