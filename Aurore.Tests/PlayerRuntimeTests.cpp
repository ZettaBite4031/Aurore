#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/UUID.hpp>

#include <Aurore/World/Player.hpp>
#include <Aurore/World/WorldManager.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
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
		using Aurore::Protocol::LoginResolution;
		using Aurore::Protocol::LoginStartRequest;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PlayerLoadedRequest;
		using Aurore::Protocol::PlayerMovementRequest;
		using Aurore::Protocol::ProtocolRequest;
		using Aurore::Protocol::ProtocolSession;
		using Aurore::Protocol::ProtocolState;
		using Aurore::Protocol::RegistryCompatibilityManifest;
		using Aurore::Util::ByteBuffer;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::Uuid;

		[[nodiscard]] PacketFrame MakeFrame(std::int32_t packet_id, const ByteBuffer& payload = {}) {
			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end()),
			};
		}

		[[nodiscard]] ResourceLocation ParseRequired(std::string_view value) {
			auto result = ResourceLocation::Parse(value);
			if (!result) throw std::runtime_error("Invalid ResourceLocation test fixture.");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistrySnapshot> BuildSyntheticSnapshot() {
			auto result = SyntheticRegistrySnapshotFactory::Build(1);
			if (!result) throw std::runtime_error("Synthetic snapshot test fixture failed.");
			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<const RegistryCompatibilityManifest> BuiltInManifest() {
			auto manifest = Aurore::Protocol::Protocol774RegistryManifest::Build();
			if (!manifest) throw std::runtime_error("Failed to build protocol-774 manifest fixture.");
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
			if (!session.HandlePacket(MakeFrame(0x00, handshake_payload)))
				throw std::runtime_error("Handshake failed.");

			ByteBuffer login_payload;
			login_payload.WriteString("PlayerOne");
			login_payload.WriteUuid({});
			auto login = session.HandlePacket(MakeFrame(Login::Serverbound::StartPacketId, login_payload));
			if (!login || login->Requests.size() != 1)
				throw std::runtime_error("Login start failed.");

			const auto login_request = GetRequest<LoginStartRequest>(login->Requests.front());
			if (!session.ResolveLogin(LoginResolution{ LoginAcceptedResolution{
				.Id = login_request.Id,
				.ProfileId = Uuid::FromOfflinePlayerName("PlayerOne"),
				.Username = "PlayerOne",
				.Properties = {},
			} }))
				throw std::runtime_error("Login resolution failed.");

			auto acknowledged = session.HandlePacket(MakeFrame(Login::Serverbound::AcknowledgedPacketId));
			if (!acknowledged || acknowledged->Requests.size() != 1)
				throw std::runtime_error("Login acknowledgement failed.");

			const auto configuration_request = GetRequest<ConfigurationStartRequest>(acknowledged->Requests.front());
			auto plan = ConfigurationSequenceBuilder::Build(BuildSyntheticSnapshot(), MakePolicy());
			if (!plan) throw std::runtime_error("Configuration plan failed.");

			if (!session.ResolveConfiguration(ConfigurationResolution{ ConfigurationReadyResolution{
				.Id = configuration_request.Id,
				.Plan = std::move(*plan),
			} }))
				throw std::runtime_error("Configuration bootstrap failed.");

			ByteBuffer known_packs_payload;
			known_packs_payload.WriteVarInt(0);
			if (!session.HandlePacket(MakeFrame(Configuration::Serverbound::SelectKnownPacksPacketId, known_packs_payload)))
				throw std::runtime_error("Known packs failed.");

			auto finish = session.HandlePacket(MakeFrame(Configuration::Serverbound::FinishConfigurationPacketId));
			if (!finish || finish->Requests.size() != 1)
				throw std::runtime_error("Configuration finish failed.");

			const auto complete = GetRequest<ConfigurationCompleteRequest>(finish->Requests.front());
			if (!session.ResolveConfiguration(ConfigurationResolution{ ConfigurationAcceptedResolution{
				.Id = complete.Id,
				.Admission = Aurore::Protocol::PlayAdmission{},
			} }))
				throw std::runtime_error("Play admission failed.");

			if (session.GetState() != ProtocolState::Play)
				throw std::runtime_error("Session did not enter Play.");
			return session;
		}
	}

	TEST(PlayerRuntimeTests, WorldManagerCreatesDistinctRuntimePlayers) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		auto* overworld = worlds.GetOverworld();
		ASSERT_NE(overworld, nullptr);

		auto* first = worlds.CreatePlayer(Uuid::FromOfflinePlayerName("Alpha"), "Alpha", *overworld);
		auto* second = worlds.CreatePlayer(Uuid::FromOfflinePlayerName("Beta"), "Beta", *overworld);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		EXPECT_NE(first->GetEntityId(), second->GetEntityId());
		EXPECT_EQ(worlds.GetPlayerCount(), 2u);
		EXPECT_EQ(first->GetDimension().GetIdentifier(), "minecraft:overworld");
		EXPECT_DOUBLE_EQ(first->GetPosition().X, 0.5);
		EXPECT_DOUBLE_EQ(first->GetPosition().Y, 64.0);
		EXPECT_DOUBLE_EQ(first->GetPosition().Z, 0.5);
	}

	TEST(PlayerRuntimeTests, PlayerAppliesPartialMovementWithoutDestroyingUnspecifiedState) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		auto* player = worlds.CreatePlayer(
			Uuid::FromOfflinePlayerName("PlayerOne"),
			"PlayerOne",
			*worlds.GetOverworld());
		ASSERT_NE(player, nullptr);

		ASSERT_TRUE(player->ApplyMovement(Aurore::World::PlayerMovementUpdate{
			.X = 12.0,
			.Y = 70.0,
			.Z = -4.0,
			.OnGround = true,
		}));
		ASSERT_TRUE(player->ApplyMovement(Aurore::World::PlayerMovementUpdate{
			.Yaw = 90.0F,
			.Pitch = -20.0F,
			.OnGround = true,
		}));

		const auto& position = player->GetPosition();
		EXPECT_DOUBLE_EQ(position.X, 12.0);
		EXPECT_DOUBLE_EQ(position.Y, 70.0);
		EXPECT_DOUBLE_EQ(position.Z, -4.0);
		EXPECT_FLOAT_EQ(position.Yaw, 90.0F);
		EXPECT_FLOAT_EQ(position.Pitch, -20.0F);
		EXPECT_TRUE(position.OnGround);
	}

	TEST(PlayerRuntimeTests, PlayerRejectsNonFiniteMovement) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		auto* player = worlds.CreatePlayer(
			Uuid::FromOfflinePlayerName("PlayerOne"),
			"PlayerOne",
			*worlds.GetOverworld());
		ASSERT_NE(player, nullptr);

		EXPECT_FALSE(player->ApplyMovement(Aurore::World::PlayerMovementUpdate{
			.X = std::numeric_limits<double>::quiet_NaN(),
		}));
		EXPECT_DOUBLE_EQ(player->GetPosition().X, 0.5);
	}

	TEST(PlayerRuntimeTests, PlayerLoadedStateIsExplicit) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		auto* player = worlds.CreatePlayer(
			Uuid::FromOfflinePlayerName("PlayerOne"),
			"PlayerOne",
			*worlds.GetOverworld());
		ASSERT_NE(player, nullptr);
		EXPECT_FALSE(player->IsLoaded());
		player->MarkLoaded();
		EXPECT_TRUE(player->IsLoaded());
	}

	TEST(PlayerRuntimeTests, WorldManagerRemovesPlayerOwnership) {
		Aurore::World::WorldManager worlds;
		ASSERT_TRUE(worlds.Initialize());
		auto* player = worlds.CreatePlayer(
			Uuid::FromOfflinePlayerName("PlayerOne"),
			"PlayerOne",
			*worlds.GetOverworld());
		ASSERT_NE(player, nullptr);
		const auto entity_id = player->GetEntityId();
		EXPECT_TRUE(worlds.RemovePlayer(entity_id));
		EXPECT_EQ(worlds.FindPlayer(entity_id), nullptr);
		EXPECT_EQ(worlds.GetPlayerCount(), 0u);
	}

	TEST(PlayerRuntimeProtocolTests, PositionMovementProducesCoreRequest) {
		auto session = EnterPlay();
		ByteBuffer payload;
		payload.WriteDouble(10.0);
		payload.WriteDouble(65.0);
		payload.WriteDouble(-3.0);
		payload.WriteUnsignedByte(0x01);

		auto result = session.HandlePacket(MakeFrame(Play::Serverbound::MovePlayerPositionPacketId, payload));
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Requests.size(), 1u);
		const auto request = GetRequest<PlayerMovementRequest>(result->Requests.front());
		ASSERT_TRUE(request.X.has_value());
		EXPECT_DOUBLE_EQ(*request.X, 10.0);
		EXPECT_TRUE(request.OnGround);
		EXPECT_FALSE(request.Yaw.has_value());
	}

	TEST(PlayerRuntimeProtocolTests, PlayerLoadedProducesCoreRequest) {
		auto session = EnterPlay();
		auto result = session.HandlePacket(MakeFrame(Play::Serverbound::PlayerLoadedPacketId));
		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Requests.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<PlayerLoadedRequest>(result->Requests.front()));
	}
}
