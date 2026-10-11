#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/UUID.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		namespace Configuration = Aurore::Protocol::Packets::Configuration;
		namespace Login = Aurore::Protocol::Packets::Login;
		namespace Play = Aurore::Protocol::Packets::Play;

		using Aurore::Core::SyntheticRegistrySnapshotFactory;

		using Aurore::Protocol::ConfigurationAcceptedResolution;
		using Aurore::Protocol::ConfigurationCompleteRequest;
		using Aurore::Protocol::ConfigurationReadyResolution;
		using Aurore::Protocol::ConfigurationResolution;
		using Aurore::Protocol::ConfigurationSequenceBuilder;
		using Aurore::Protocol::ConfigurationSequencePolicy;
		using Aurore::Protocol::ConfigurationStartRequest;
		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::LoginAcceptedResolution;
		using Aurore::Protocol::LoginStartRequest;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolRequest;
		using Aurore::Protocol::ProtocolSession;
		using Aurore::Protocol::ProtocolState;
		using Aurore::Protocol::RegistryCompatibilityManifest;

		using Aurore::Util::ByteBuffer;
		using Aurore::Util::ByteReader;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::Uuid;

		[[nodiscard]] PacketFrame MakeFrame(
			std::int32_t packet_id,
			const ByteBuffer& payload = {}) {

			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end()),
			};
		}

		[[nodiscard]] ResourceLocation ParseRequired(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			if (!result)
				throw std::runtime_error("Invalid ResourceLocation test fixture.");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> BuildSyntheticSnapshot() {
			auto result = SyntheticRegistrySnapshotFactory::Build(1);
			if (!result)
				throw std::runtime_error("Synthetic snapshot test fixture failed.");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistryCompatibilityManifest> BuiltInManifest() {
			auto manifest = Aurore::Protocol::Protocol774RegistryManifest::Build();
			if (!manifest)
				throw std::runtime_error("Failed to build protocol-774 manifest fixture.");
			return std::make_shared<const RegistryCompatibilityManifest>(std::move(*manifest));
		}

		[[nodiscard]] ConfigurationSequencePolicy MakePolicy() {
			return ConfigurationSequencePolicy{
				.EnabledFeatures = { ParseRequired("minecraft:vanilla") },
				.KnownPacks = {},
				.RegistryManifest = BuiltInManifest(),
			};
		}

		template<typename TRequest>
		[[nodiscard]] TRequest GetRequest(const ProtocolRequest& request) {
			if (!std::holds_alternative<TRequest>(request))
				throw std::runtime_error("Unexpected protocol request type.");
			return std::get<TRequest>(request);
		}

		[[nodiscard]] ProtocolSession EnterPlay() {
			ProtocolSession session;

			ByteBuffer handshake_payload;
			handshake_payload.WriteVarInt(774);
			handshake_payload.WriteString("localhost");
			handshake_payload.WriteUInt16(25565);
			handshake_payload.WriteVarInt(static_cast<std::int32_t>(HandshakeIntention::Login));

			auto handshake = session.HandlePacket(MakeFrame(0x00, handshake_payload));
			if (!handshake)
				throw std::runtime_error("Play fixture handshake failed.");

			ByteBuffer login_payload;
			login_payload.WriteString("PlayerOne");
			login_payload.WriteUuid({});

			auto login = session.HandlePacket(MakeFrame(Login::Serverbound::StartPacketId, login_payload));
			if (!login || login->Requests.size() != 1)
				throw std::runtime_error("Play fixture Login start failed.");

			const auto login_request = GetRequest<LoginStartRequest>(login->Requests.front());
			auto login_resolution = session.ResolveLogin(Aurore::Protocol::LoginResolution{ LoginAcceptedResolution{
				.Id = login_request.Id,
				.ProfileId = Uuid::FromOfflinePlayerName("PlayerOne"),
				.Username = "PlayerOne",
				.Properties = {},
			} });
			if (!login_resolution)
				throw std::runtime_error("Play fixture Login resolution failed.");

			auto acknowledged = session.HandlePacket(MakeFrame(Login::Serverbound::AcknowledgedPacketId));
			if (!acknowledged || acknowledged->Requests.size() != 1)
				throw std::runtime_error("Play fixture Login acknowledgement failed.");

			const auto configuration_request = GetRequest<ConfigurationStartRequest>(acknowledged->Requests.front());
			auto plan = ConfigurationSequenceBuilder::Build(BuildSyntheticSnapshot(), MakePolicy());
			if (!plan)
				throw std::runtime_error("Play fixture Configuration plan failed.");

			auto ready = session.ResolveConfiguration(ConfigurationResolution{
				ConfigurationReadyResolution{
					.Id = configuration_request.Id,
					.Plan = std::move(*plan),
				}
			});
			if (!ready)
				throw std::runtime_error("Play fixture Configuration bootstrap failed.");

			ByteBuffer known_packs_payload;
			known_packs_payload.WriteVarInt(0);
			auto known_packs = session.HandlePacket(MakeFrame(
				Configuration::Serverbound::SelectKnownPacksPacketId,
				known_packs_payload));
			if (!known_packs)
				throw std::runtime_error("Play fixture Known Packs failed.");

			auto finish = session.HandlePacket(MakeFrame(
				Configuration::Serverbound::FinishConfigurationPacketId));
			if (!finish || finish->Requests.size() != 1)
				throw std::runtime_error("Play fixture Configuration finish failed.");

			const auto complete_request = GetRequest<ConfigurationCompleteRequest>(finish->Requests.front());
			auto accepted = session.ResolveConfiguration(ConfigurationResolution{
				ConfigurationAcceptedResolution{ .Id = complete_request.Id }
			});
			if (!accepted || session.GetState() != ProtocolState::Play)
				throw std::runtime_error("Play fixture admission failed.");

			return session;
		}
	}

	TEST(PlayProtocolTests, UsesProtocol774FirstLightPacketIds) {
		EXPECT_EQ(Play::Serverbound::AcceptTeleportationPacketId, 0x00);
		EXPECT_EQ(Play::Serverbound::ClientTickEndPacketId, 0x0C);
		EXPECT_EQ(Play::Serverbound::KeepAlivePacketId, 0x1B);
		EXPECT_EQ(Play::Serverbound::MovePlayerPositionPacketId, 0x1D);
		EXPECT_EQ(Play::Serverbound::MovePlayerPositionRotationPacketId, 0x1E);
		EXPECT_EQ(Play::Serverbound::MovePlayerRotationPacketId, 0x1F);
		EXPECT_EQ(Play::Serverbound::MovePlayerStatusOnlyPacketId, 0x20);
		EXPECT_EQ(Play::Serverbound::PlayerLoadedPacketId, 0x2B);
	}

	TEST(PlayProtocolTests, DecodesTeleportAcknowledgement) {
		ByteBuffer payload;
		payload.WriteVarInt(42);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::AcceptTeleportationPacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::AcceptTeleportation>(*result));
		EXPECT_EQ(std::get<Play::Serverbound::AcceptTeleportation>(*result).TeleportId, 42);
	}

	TEST(PlayProtocolTests, DecodesKeepAliveResponse) {
		ByteBuffer payload;
		payload.WriteInt64(-1234567890123LL);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::KeepAlivePacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::KeepAlive>(*result));
		EXPECT_EQ(std::get<Play::Serverbound::KeepAlive>(*result).Id, -1234567890123LL);
	}

	TEST(PlayProtocolTests, DecodesPositionAndMovementFlags) {
		ByteBuffer payload;
		payload.WriteDouble(12.5);
		payload.WriteDouble(64.0);
		payload.WriteDouble(-7.25);
		payload.WriteUnsignedByte(0x03);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::MovePlayerPositionPacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		const auto& packet = std::get<Play::Serverbound::MovePlayerPosition>(*result);
		EXPECT_DOUBLE_EQ(packet.X, 12.5);
		EXPECT_DOUBLE_EQ(packet.Y, 64.0);
		EXPECT_DOUBLE_EQ(packet.Z, -7.25);
		EXPECT_TRUE(packet.Flags.OnGround);
		EXPECT_TRUE(packet.Flags.HorizontalCollision);
	}

	TEST(PlayProtocolTests, DecodesPositionRotationAndMovementFlags) {
		ByteBuffer payload;
		payload.WriteDouble(1.0);
		payload.WriteDouble(70.0);
		payload.WriteDouble(2.0);
		payload.WriteFloat(90.0F);
		payload.WriteFloat(-15.0F);
		payload.WriteUnsignedByte(0x01);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::MovePlayerPositionRotationPacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		const auto& packet = std::get<Play::Serverbound::MovePlayerPositionRotation>(*result);
		EXPECT_DOUBLE_EQ(packet.X, 1.0);
		EXPECT_DOUBLE_EQ(packet.Y, 70.0);
		EXPECT_DOUBLE_EQ(packet.Z, 2.0);
		EXPECT_FLOAT_EQ(packet.Yaw, 90.0F);
		EXPECT_FLOAT_EQ(packet.Pitch, -15.0F);
		EXPECT_TRUE(packet.Flags.OnGround);
		EXPECT_FALSE(packet.Flags.HorizontalCollision);
	}

	TEST(PlayProtocolTests, DecodesRotationOnlyMovement) {
		ByteBuffer payload;
		payload.WriteFloat(180.0F);
		payload.WriteFloat(20.0F);
		payload.WriteUnsignedByte(0x02);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::MovePlayerRotationPacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		const auto& packet = std::get<Play::Serverbound::MovePlayerRotation>(*result);
		EXPECT_FLOAT_EQ(packet.Yaw, 180.0F);
		EXPECT_FLOAT_EQ(packet.Pitch, 20.0F);
		EXPECT_FALSE(packet.Flags.OnGround);
		EXPECT_TRUE(packet.Flags.HorizontalCollision);
	}

	TEST(PlayProtocolTests, DecodesStatusOnlyMovement) {
		ByteBuffer payload;
		payload.WriteUnsignedByte(0x00);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::MovePlayerStatusOnlyPacketId,
			payload));

		ASSERT_TRUE(result.has_value());
		const auto& packet = std::get<Play::Serverbound::MovePlayerStatusOnly>(*result);
		EXPECT_FALSE(packet.Flags.OnGround);
		EXPECT_FALSE(packet.Flags.HorizontalCollision);
	}

	TEST(PlayProtocolTests, AcceptsEmptyTickEndAndPlayerLoaded) {
		const auto tick_end = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::ClientTickEndPacketId));
		const auto player_loaded = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::PlayerLoadedPacketId));

		ASSERT_TRUE(tick_end.has_value());
		ASSERT_TRUE(player_loaded.has_value());
		EXPECT_TRUE(std::holds_alternative<Play::Serverbound::ClientTickEnd>(*tick_end));
		EXPECT_TRUE(std::holds_alternative<Play::Serverbound::PlayerLoaded>(*player_loaded));
	}

	TEST(PlayProtocolTests, RejectsTrailingDataForEmptyPackets) {
		ByteBuffer payload;
		payload.WriteUnsignedByte(0xAA);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::ClientTickEndPacketId,
			payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
	}

	TEST(PlayProtocolTests, RejectsTruncatedMovementPacket) {
		ByteBuffer payload;
		payload.WriteDouble(1.0);

		const auto result = Play::Serverbound::Decode(MakeFrame(
			Play::Serverbound::MovePlayerPositionPacketId,
			payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
	}

	TEST(PlayProtocolTests, RejectsUnknownPacketId) {
		const auto result = Play::Serverbound::Decode(MakeFrame(0x7F));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
	}

	TEST(PlayProtocolSessionTests, DispatchesKnownPacketAfterPlayAdmission) {
		auto session = EnterPlay();

		const auto result = session.HandlePacket(MakeFrame(
			Play::Serverbound::ClientTickEndPacketId));

		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(result->OutboundPackets.empty());
		EXPECT_TRUE(result->Requests.empty());
		EXPECT_FALSE(result->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Play);
	}

	TEST(PlayProtocolSessionTests, RejectsUnknownPlayPacketWithoutChangingState) {
		auto session = EnterPlay();

		const auto result = session.HandlePacket(MakeFrame(0x7F));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
		EXPECT_EQ(session.GetState(), ProtocolState::Play);
	}
}
