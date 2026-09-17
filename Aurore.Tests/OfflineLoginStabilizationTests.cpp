#include <Aurore/Core/ClientLifecycle.hpp>
#include <Aurore/Core/ClientLogin.hpp>

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/ProtocolConnection.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/UUID.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using Aurore::Core::ClientConfiguration;
		using Aurore::Core::ClientConnection;
		using Aurore::Core::ClientError;
		using Aurore::Core::ClientIdentity;
		using Aurore::Core::ClientLifecycleState;
		using Aurore::Core::ClientLogin;
		using Aurore::Core::ConnectionManager;

		using Aurore::Network::ConnectionId;
		using Aurore::Network::NetworkEndpoint;

		using Aurore::Protocol::ConnectionDisposition;
		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::LoginAcceptedResolution;
		using Aurore::Protocol::LoginRejectedResolution;
		using Aurore::Protocol::LoginStartRequest;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PacketFrameDecoder;
		using Aurore::Protocol::ProtocolConnection;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolRequest;
		using Aurore::Protocol::ProtocolState;

		using Aurore::Util::ByteBuffer;
		using Aurore::Util::ByteReader;
		using Aurore::Util::Uuid;

		namespace Login =
			Aurore::Protocol::Packets::Login;

		ByteBuffer MakeLoginHandshake() {
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

		ByteBuffer MakeLoginStart(
			std::string_view username,
			const Uuid& presented_profile_id = {}) {

			ByteBuffer payload;
			payload.WriteString(username);
			payload.WriteUuid(presented_profile_id);

			return EncodePacketFrame(
				Login::Serverbound::StartPacketId,
				payload.Bytes());
		}

		ByteBuffer MakeLoginAcknowledged() {
			return EncodePacketFrame(
				Login::Serverbound::AcknowledgedPacketId,
				std::span<const std::byte>{});
		}

		std::vector<std::byte> Combine(
			std::span<const std::byte> first,
			std::span<const std::byte> second) {

			std::vector<std::byte> result;
			result.reserve(first.size() + second.size());
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

		PacketFrame DecodeSingleFrame(
			const ByteBuffer& encoded) {

			PacketFrameDecoder decoder;
			decoder.Append(encoded.Bytes());

			auto decoded = decoder.TryDecode();

			EXPECT_TRUE(decoded.has_value());
			EXPECT_TRUE(decoded->has_value());
			EXPECT_TRUE(decoder.Empty());

			if (!decoded || !decoded->has_value()) {
				return {};
			}

			return std::move(decoded->value());
		}

		LoginStartRequest GetLoginRequest(
			const ProtocolRequest& request) {

			EXPECT_TRUE(
				std::holds_alternative<
					LoginStartRequest>(request));

			return std::get<LoginStartRequest>(request);
		}

		ClientConnection& OpenLoginClient(
			ConnectionManager& manager,
			ConnectionId connection,
			std::string_view username,
			ClientConnection::TimePoint observed_at =
				ClientConnection::Clock::now()) {

			auto opened = manager.Open(
				connection,
				NetworkEndpoint{
					.Address = "127.0.0.1",
					.Port = 25565,
				},
				NetworkEndpoint{
					.Address = "127.0.0.1",
					.Port = static_cast<std::uint16_t>(
						50000 + connection.Value),
				},
				{},
				observed_at);

			EXPECT_TRUE(opened.has_value());

			auto& client = opened->get();

			const auto received = Combine(
				MakeLoginHandshake().Bytes(),
				MakeLoginStart(username).Bytes());

			const auto result = client.Receive(
				received,
				observed_at);

			EXPECT_FALSE(result.HasError());
			EXPECT_EQ(result.Requests.size(), 1);
			EXPECT_EQ(
				client.GetLifecycleState(),
				ClientLifecycleState::Login);

			return client;
		}
	}

	TEST(UuidStabilizationTests, ProducesKnownOfflinePlayerUuid) {
		EXPECT_EQ(
			Uuid::FromOfflinePlayerName("Notch").ToString(),
			"B50AD385-829D-3141-A216-7E7D7539BA7F");

		EXPECT_EQ(
			Uuid::FromOfflinePlayerName("Steve").ToString(),
			"5627DD98-E6BE-3C21-B8A8-E92344183641");
	}

	TEST(UuidStabilizationTests, OfflineNamesRemainCaseSensitive) {
		EXPECT_NE(
			Uuid::FromOfflinePlayerName("Player"),
			Uuid::FromOfflinePlayerName("player"));
	}

	TEST(ClientLoginStabilizationTests, RejectsEmptyAndNonPrintableNames) {
		EXPECT_FALSE(ClientLogin::IsUsernameValid(""));
		EXPECT_FALSE(ClientLogin::IsUsernameValid("Bad Name"));
		EXPECT_FALSE(ClientLogin::IsUsernameValid(
			std::string(17, 'a')));
		EXPECT_TRUE(ClientLogin::IsUsernameValid("Player_01"));
	}

	TEST(OfflineLoginStabilizationTests, CompletesOfflineLoginIntoConfiguration) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 8,
				.MaximumPlayers = 8,
			}
		};

		const auto start_time =
			ClientConnection::TimePoint{};

		auto opened = manager.Open(
			ConnectionId{ 1 },
			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 25565,
			},
			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 50001,
			},
			{},
			start_time);

		ASSERT_TRUE(opened.has_value());

		auto& client = opened->get();

		const auto login_bytes = Combine(
			MakeLoginHandshake().Bytes(),
			MakeLoginStart("PlayerOne").Bytes());

		const auto login_start_result =
			client.Receive(
				login_bytes,
				start_time + std::chrono::seconds{ 1 });

		ASSERT_FALSE(login_start_result.HasError());
		ASSERT_EQ(login_start_result.Requests.size(), 1);

		const auto request = GetLoginRequest(
			login_start_result.Requests.front());

		ClientLogin login;
		auto identity = login.ResolveOffline(request);

		ASSERT_TRUE(identity.has_value());

		const auto expected_uuid =
			Uuid::FromOfflinePlayerName("PlayerOne");

		EXPECT_EQ(identity->UniqueId, expected_uuid);

		auto admission = manager.AdmitIdentity(
			ConnectionId{ 1 },
			std::move(*identity));

		ASSERT_TRUE(admission.has_value());
		ASSERT_TRUE(client.GetIdentity().has_value());

		const auto resolution_result =
			client.ResolveLogin(
				LoginAcceptedResolution{
					.Id = request.Id,
					.ProfileId =
						client.GetIdentity()->UniqueId,
					.Username =
						client.GetIdentity()->Username,
					.Properties =
						client.GetIdentity()->Properties,
				},
				start_time + std::chrono::seconds{ 2 });

		ASSERT_FALSE(resolution_result.HasError());
		ASSERT_EQ(resolution_result.OutboundFrames.size(), 1);
		EXPECT_EQ(
			resolution_result.Disposition,
			Aurore::Core::ClientReceiveDisposition::KeepOpen);

		const auto success_frame = DecodeSingleFrame(
			resolution_result.OutboundFrames.front());

		ASSERT_EQ(
			success_frame.PacketId,
			Login::Clientbound::SuccessPacketId);

		ByteReader success_reader{
			success_frame.Payload
		};

		const auto encoded_uuid =
			success_reader.ReadUuid();

		const auto encoded_username =
			success_reader.ReadString(16);

		const auto property_count =
			success_reader.ReadVarInt();

		ASSERT_TRUE(encoded_uuid.has_value());
		ASSERT_TRUE(encoded_username.has_value());
		ASSERT_TRUE(property_count.has_value());

		EXPECT_EQ(*encoded_uuid, expected_uuid);
		EXPECT_EQ(*encoded_username, "PlayerOne");
		EXPECT_EQ(*property_count, 0);
		EXPECT_TRUE(success_reader.Empty());

		const auto acknowledgement_result =
			client.Receive(
				MakeLoginAcknowledged().Bytes(),
				start_time + std::chrono::seconds{ 3 });

		ASSERT_FALSE(acknowledgement_result.HasError());
		EXPECT_EQ(
			client.GetProtocolState(),
			ProtocolState::Configuration);
		EXPECT_EQ(
			client.GetLifecycleState(),
			ClientLifecycleState::Configuration);
		EXPECT_EQ(manager.AuthenticatedCount(), 1);
	}

	TEST(OfflineLoginStabilizationTests, RejectedResolutionSendsDisconnectAndClosesAfterFlush) {
		ProtocolConnection connection;

		const auto login_start_result = connection.Receive(
			Combine(
				MakeLoginHandshake().Bytes(),
				MakeLoginStart("PlayerOne").Bytes()));

		ASSERT_FALSE(login_start_result.HasError());
		ASSERT_EQ(login_start_result.Requests.size(), 1);

		const auto request = GetLoginRequest(
			login_start_result.Requests.front());

		const auto rejection_result = connection.ResolveLogin(
			LoginRejectedResolution{
				.Id = request.Id,
				.ReasonJson =
					R"({"text":"Rejected for testing."})",
			});

		ASSERT_FALSE(rejection_result.HasError());
		ASSERT_EQ(rejection_result.OutboundFrames.size(), 1);
		EXPECT_EQ(
			rejection_result.Disposition,
			ConnectionDisposition::CloseAfterFlush);

		const auto disconnect_frame = DecodeSingleFrame(
			rejection_result.OutboundFrames.front());

		EXPECT_EQ(
			disconnect_frame.PacketId,
			Login::Clientbound::DisconnectPacketId);

		ByteReader reader{ disconnect_frame.Payload };
		const auto reason = reader.ReadString(1024);

		ASSERT_TRUE(reason.has_value());
		EXPECT_EQ(
			*reason,
			R"({"text":"Rejected for testing."})");
		EXPECT_TRUE(reader.Empty());
	}

	TEST(OfflineLoginStabilizationTests, RejectsStaleLoginResolution) {
		ProtocolConnection connection;

		const auto login_start_result = connection.Receive(
			Combine(
				MakeLoginHandshake().Bytes(),
				MakeLoginStart("PlayerOne").Bytes()));

		ASSERT_FALSE(login_start_result.HasError());
		ASSERT_EQ(login_start_result.Requests.size(), 1);

		const auto request = GetLoginRequest(
			login_start_result.Requests.front());

		const auto result = connection.ResolveLogin(
			LoginAcceptedResolution{
				.Id = {
					.Value = request.Id.Value + 1,
				},
				.ProfileId =
					Uuid::FromOfflinePlayerName("PlayerOne"),
				.Username = "PlayerOne",
				.Properties = {},
			});

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(
			std::holds_alternative<ProtocolError>(
				*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::InvalidRequestResolution);
		EXPECT_EQ(
			result.Disposition,
			ConnectionDisposition::CloseImmediately);
	}

	TEST(OfflineLoginStabilizationTests, RejectsAcknowledgementBeforeSuccess) {
		ProtocolConnection connection;

		const auto handshake_result = connection.Receive(
			MakeLoginHandshake().Bytes());

		ASSERT_FALSE(handshake_result.HasError());

		const auto result = connection.Receive(
			MakeLoginAcknowledged().Bytes());

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(
			std::holds_alternative<ProtocolError>(
				*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::UnexpectedPacket);
	}

	TEST(ConnectionManagerStabilizationTests, DuplicateUuidDoesNotLeakUsernameIndex) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 8,
				.MaximumPlayers = 8,
			}
		};

		auto& first = OpenLoginClient(
			manager,
			ConnectionId{ 1 },
			"FirstPlayer");

		auto& second = OpenLoginClient(
			manager,
			ConnectionId{ 2 },
			"SecondPlayer");

		const auto shared_uuid =
			Uuid::FromOfflinePlayerName("shared");

		ASSERT_TRUE(manager.AdmitIdentity(
			ConnectionId{ 1 },
			ClientIdentity{
				.Username = "FirstPlayer",
				.UniqueId = shared_uuid,
				.Properties = {},
			}));

		const auto duplicate_result =
			manager.AdmitIdentity(
				ConnectionId{ 2 },
				ClientIdentity{
					.Username = "SecondPlayer",
					.UniqueId = shared_uuid,
					.Properties = {},
				});

		ASSERT_FALSE(duplicate_result.has_value());
		EXPECT_EQ(
			duplicate_result.error(),
			ClientError::DuplicateUniqueId);

		EXPECT_EQ(
			manager.FindByUsername("SecondPlayer"),
			nullptr);

		const auto second_uuid =
			Uuid::FromOfflinePlayerName("SecondPlayer");

		const auto retry_result =
			manager.AdmitIdentity(
				ConnectionId{ 2 },
				ClientIdentity{
					.Username = "SecondPlayer",
					.UniqueId = second_uuid,
					.Properties = {},
				});

		EXPECT_TRUE(retry_result.has_value());
		EXPECT_EQ(manager.AuthenticatedCount(), 2);

		(void)first;
		(void)second;
	}

	TEST(ConnectionManagerStabilizationTests, DuplicateUsernameComparisonIsAsciiCaseInsensitive) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 8,
				.MaximumPlayers = 8,
			}
		};

		OpenLoginClient(
			manager,
			ConnectionId{ 1 },
			"PlayerOne");

		OpenLoginClient(
			manager,
			ConnectionId{ 2 },
			"playerone");

		ASSERT_TRUE(manager.AdmitIdentity(
			ConnectionId{ 1 },
			ClientIdentity{
				.Username = "PlayerOne",
				.UniqueId =
					Uuid::FromOfflinePlayerName("PlayerOne"),
				.Properties = {},
			}));

		const auto result = manager.AdmitIdentity(
			ConnectionId{ 2 },
			ClientIdentity{
				.Username = "playerone",
				.UniqueId =
					Uuid::FromOfflinePlayerName("playerone"),
				.Properties = {},
			});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ClientError::DuplicateUsername);
	}

	TEST(ConnectionManagerStabilizationTests, EnforcesAuthenticatedPlayerCapacitySeparately) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 4,
				.MaximumPlayers = 1,
			}
		};

		OpenLoginClient(
			manager,
			ConnectionId{ 1 },
			"First");

		OpenLoginClient(
			manager,
			ConnectionId{ 2 },
			"Second");

		ASSERT_TRUE(manager.AdmitIdentity(
			ConnectionId{ 1 },
			ClientIdentity{
				.Username = "First",
				.UniqueId =
					Uuid::FromOfflinePlayerName("First"),
				.Properties = {},
			}));

		const auto result = manager.AdmitIdentity(
			ConnectionId{ 2 },
			ClientIdentity{
				.Username = "Second",
				.UniqueId =
					Uuid::FromOfflinePlayerName("Second"),
				.Properties = {},
			});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ClientError::PlayerCapacityExceeded);
	}

	TEST(ConnectionManagerStabilizationTests, RemovingConnectionReleasesIdentityIndexes) {
		ConnectionManager manager;

		OpenLoginClient(
			manager,
			ConnectionId{ 1 },
			"PlayerOne");

		const auto uuid =
			Uuid::FromOfflinePlayerName("PlayerOne");

		ASSERT_TRUE(manager.AdmitIdentity(
			ConnectionId{ 1 },
			ClientIdentity{
				.Username = "PlayerOne",
				.UniqueId = uuid,
				.Properties = {},
			}));

		ASSERT_EQ(manager.AuthenticatedCount(), 1);
		ASSERT_NE(manager.FindByUsername("playerone"), nullptr);
		ASSERT_NE(manager.FindByUniqueId(uuid), nullptr);

		ASSERT_TRUE(manager.Remove(ConnectionId{ 1 }));

		EXPECT_EQ(manager.AuthenticatedCount(), 0);
		EXPECT_EQ(manager.FindByUsername("PlayerOne"), nullptr);
		EXPECT_EQ(manager.FindByUniqueId(uuid), nullptr);
	}

	TEST(ClientLifecycleStabilizationTests, LoginActivityDoesNotResetStateEntryDeadline) {
		const auto connected_at =
			ClientConnection::TimePoint{};

		ClientConnection client{
			ConnectionId{ 1 },
			{},
			{},
			{},
			{},
			connected_at,
		};

		const auto entered_login_at =
			connected_at + std::chrono::seconds{ 1 };

		const auto login_start_result = client.Receive(
			Combine(
				MakeLoginHandshake().Bytes(),
				MakeLoginStart("PlayerOne").Bytes()),
			entered_login_at);

		ASSERT_FALSE(login_start_result.HasError());
		ASSERT_EQ(login_start_result.Requests.size(), 1);
		EXPECT_EQ(
			client.GetStateEnteredAt(),
			entered_login_at);

		const auto request = GetLoginRequest(
			login_start_result.Requests.front());

		const auto resolution_result = client.ResolveLogin(
			LoginAcceptedResolution{
				.Id = request.Id,
				.ProfileId =
					Uuid::FromOfflinePlayerName("PlayerOne"),
				.Username = "PlayerOne",
				.Properties = {},
			},
			entered_login_at + std::chrono::seconds{ 20 });

		ASSERT_FALSE(resolution_result.HasError());
		EXPECT_EQ(
			client.GetStateEnteredAt(),
			entered_login_at);

		const ClientConfiguration configuration{
			.MaximumClients = 8,
			.MaximumPlayers = 8,
			.LoginTimeout = std::chrono::seconds{ 30 },
		};

		const auto timeout = client.CheckTimeout(
			configuration,
			entered_login_at + std::chrono::seconds{ 30 });

		ASSERT_TRUE(timeout.has_value());
		EXPECT_EQ(
			timeout->Cause,
			Aurore::Core::ClientCloseCause::LoginTimeout);
	}
}
