#include <Aurore/Core/ClientLifecycle.hpp>

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/ProtocolConnection.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/UUID.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Core::ClientConnection;
		using Aurore::Core::ClientLifecycleState;

		using Aurore::Network::ConnectionId;
		using Aurore::Network::NetworkEndpoint;

		using Aurore::Protocol::ConnectionDisposition;
		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::LoginStartRequest;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ProtocolConnection;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolState;

		using Aurore::Util::ByteBuffer;
		using Aurore::Util::Uuid;

		namespace Login =
			Aurore::Protocol::Packets::Login;

		Uuid MakeProfileId() {
			Uuid::Storage bytes{};

			for (std::size_t index{ 0 };
				index < bytes.size();
				++index) {

				bytes[index] =
					static_cast<std::byte>(
						index + 1);
			}

			return Uuid{ bytes };
		}

		PacketFrame MakeLoginStartFrame(
			std::string_view username,
			const Uuid& profile_id,
			std::span<const std::byte>
			trailing_data = {}) {

			ByteBuffer payload;

			payload.WriteString(username);
			payload.WriteBytes(
				profile_id.Bytes());

			payload.WriteBytes(
				trailing_data);

			return PacketFrame{
				.PacketId =
					Login::Serverbound::
						StartPacketId,

				.Payload =
					std::vector<std::byte>(
						payload.Bytes().begin(),
						payload.Bytes().end()),
			};
		}

		ByteBuffer MakeEncodedLoginHandshake() {
			ByteBuffer payload;

			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(25565);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					HandshakeIntention::Login));

			return EncodePacketFrame(
				0x00,
				payload.Bytes());
		}

		ByteBuffer MakeEncodedLoginStart(
			std::string_view username,
			const Uuid& profile_id) {

			const auto frame =
				MakeLoginStartFrame(
					username,
					profile_id);

			return EncodePacketFrame(
				frame.PacketId,
				frame.Payload);
		}

		std::vector<std::byte> Combine(
			std::span<const std::byte> first,
			std::span<const std::byte> second) {

			std::vector<std::byte> result;

			result.reserve(
				first.size() +
				second.size());

			result.insert(
				result.end(),
				first.begin(),
				first.end());

			result.insert(
				result.end(),
				second.begin(),
				second.end());

			return result;
		}
	}

	TEST(LoginCodecTests, DecodesLoginStart) {
		const auto profile_id =
			MakeProfileId();

		const auto result =
			Login::Serverbound::Decode(
				MakeLoginStartFrame(
					"PlayerOne",
					profile_id));

		ASSERT_TRUE(result.has_value());

		ASSERT_TRUE(
			std::holds_alternative<
			Login::Serverbound::Start>(
				*result));

		const auto& packet =
			std::get<
			Login::Serverbound::Start>(
				*result);

		EXPECT_EQ(
			packet.Username,
			"PlayerOne");

		EXPECT_EQ(
			packet.PresentedProfileId,
			profile_id);
	}

	TEST(LoginCodecTests, RejectsOversizedUsername) {
		const std::string username(
			Login::Serverbound::
			MaximumUsernameEncodedBytes + 1,
			'a');

		const auto result =
			Login::Serverbound::Decode(
				MakeLoginStartFrame(
					username,
					MakeProfileId()));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(LoginCodecTests, RejectsTruncatedProfileId) {
		ByteBuffer payload;

		payload.WriteString("PlayerOne");

		const std::array<std::byte, 8>
			partial_uuid{};

		payload.WriteBytes(partial_uuid);

		const PacketFrame frame{
			.PacketId =
				Login::Serverbound::
					StartPacketId,

			.Payload =
				std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end()),
		};

		const auto result =
			Login::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(LoginCodecTests, RejectsTrailingData) {
		const std::byte trailing{
			0x7F
		};

		const auto result =
			Login::Serverbound::Decode(
				MakeLoginStartFrame(
					"PlayerOne",
					MakeProfileId(),
					std::span<const std::byte>(
						&trailing,
						1)));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error(),
			ProtocolError::TrailingPacketData);
	}

	TEST(
		ProtocolConnectionLoginTests,
		ProducesCoreRequestAndWaitsForDecision) {

		const auto profile_id =
			MakeProfileId();

		const auto handshake =
			MakeEncodedLoginHandshake();

		const auto login_start =
			MakeEncodedLoginStart(
				"PlayerOne",
				profile_id);

		const auto received =
			Combine(
				handshake.Bytes(),
				login_start.Bytes());

		ProtocolConnection connection;

		const auto result =
			connection.Receive(received);

		ASSERT_FALSE(result.HasError());

		EXPECT_TRUE(
			result.OutboundFrames.empty());

		ASSERT_EQ(
			result.Requests.size(),
			1);

		EXPECT_EQ(
			result.Disposition,
			ConnectionDisposition::KeepOpen);

		EXPECT_EQ(
			connection.GetSession().GetState(),
			ProtocolState::Login);

		ASSERT_TRUE(
			std::holds_alternative<
			LoginStartRequest>(
				result.Requests.front()));

		const auto& request =
			std::get<LoginStartRequest>(
				result.Requests.front());

		EXPECT_TRUE(
			static_cast<bool>(request.Id));

		EXPECT_EQ(
			request.Username,
			"PlayerOne");

		EXPECT_EQ(
			request.PresentedProfileId,
			profile_id);

		ASSERT_TRUE(
			connection.GetSession()
			.GetPendingRequestId()
			.has_value());

		EXPECT_EQ(
			*connection.GetSession()
			.GetPendingRequestId(),
			request.Id);
	}

	TEST(
		ProtocolConnectionLoginTests,
		RejectsSecondLoginStartWhileDecisionIsPending) {

		const auto profile_id =
			MakeProfileId();

		ProtocolConnection connection;

		const auto first_result =
			connection.Receive(
				Combine(
					MakeEncodedLoginHandshake()
					.Bytes(),

					MakeEncodedLoginStart(
						"PlayerOne",
						profile_id)
					.Bytes()));

		ASSERT_FALSE(
			first_result.HasError());

		ASSERT_EQ(
			first_result.Requests.size(),
			1);

		const auto second_packet =
			MakeEncodedLoginStart(
				"PlayerOne",
				profile_id);

		const auto second_result =
			connection.Receive(
				second_packet.Bytes());

		ASSERT_TRUE(
			second_result.HasError());

		ASSERT_TRUE(
			std::holds_alternative<
			ProtocolError>(
				*second_result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(
				*second_result.Error),
			ProtocolError::UnexpectedPacket);

		EXPECT_TRUE(
			second_result.Requests.empty());

		EXPECT_EQ(
			second_result.Disposition,
			ConnectionDisposition::
			CloseImmediately);
	}

	TEST(
		ClientLifecycleLoginTests,
		PropagatesLoginRequestIntoCore) {

		const auto profile_id =
			MakeProfileId();

		ClientConnection client{
			ConnectionId{ 1 },

			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 25565,
			},

			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 50000,
			},

			{},

			{},

			std::chrono::steady_clock::now(),
		};

		const auto result =
			client.Receive(
				Combine(
					MakeEncodedLoginHandshake()
					.Bytes(),

					MakeEncodedLoginStart(
						"PlayerOne",
						profile_id)
					.Bytes()));

		ASSERT_FALSE(result.HasError());

		ASSERT_EQ(
			result.Requests.size(),
			1);

		EXPECT_EQ(
			client.GetLifecycleState(),
			ClientLifecycleState::Login);

		const auto& request =
			std::get<LoginStartRequest>(
				result.Requests.front());

		EXPECT_EQ(
			request.Username,
			"PlayerOne");

		EXPECT_EQ(
			request.PresentedProfileId,
			profile_id);
	}
}
