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
#include <array>
#include <set>
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
		using Aurore::Protocol::PlayerInputRequest;
		using Aurore::Protocol::PreparedPacketFrame;
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

		struct PlayAdmissionFixture final {
			ProtocolSession Session;
			Aurore::Protocol::ProtocolAction Admission;
		};

		[[nodiscard]] PlayAdmissionFixture EnterPlayWithAdmission(
			std::vector<Aurore::Protocol::PlayChunkColumn> initial_chunks = {}) {
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
			auto login_resolution = session.ResolveLogin(Aurore::Protocol::LoginResolution{
				LoginAcceptedResolution{
					.Id = login_request.Id,
					.ProfileId = Uuid::FromOfflinePlayerName("PlayerOne"),
					.Username = "PlayerOne",
					.Properties = {},
				}
			});
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
				ConfigurationAcceptedResolution{
					.Id = complete_request.Id,
					.Admission = Aurore::Protocol::PlayAdmission{
						.InitialChunks = std::move(initial_chunks),
					},
				}
			});
			if (!accepted || session.GetState() != ProtocolState::Play)
				throw std::runtime_error("Play fixture admission failed.");

			return PlayAdmissionFixture{
				.Session = std::move(session),
				.Admission = std::move(*accepted),
			};
		}

		[[nodiscard]] ProtocolSession EnterPlay() {
			return std::move(EnterPlayWithAdmission().Session);
		}

		[[nodiscard]] const PacketFrame& GetPreparedFrame(
			const Aurore::Protocol::ClientboundPacket& packet) {
			if (!std::holds_alternative<PreparedPacketFrame>(packet))
				throw std::runtime_error("Expected prepared Play frame.");
			return std::get<PreparedPacketFrame>(packet).Frame;
		}
	}

	/* The client renderer needs eight horizontally adjacent, lit columns. */
	[[nodiscard]] std::vector<Aurore::Protocol::PlayChunkColumn> MakeNineChunkColumns() {
		std::vector<Aurore::Protocol::PlayChunkColumn> result;
		result.reserve(9);
		const auto air = ParseRequired("minecraft:air");
		const auto plains = ParseRequired("minecraft:plains");

		for (std::int32_t z = -1; z <= 1; ++z) {
			for (std::int32_t x = -1; x <= 1; ++x) {
				if (x == 0 && z == 0) continue;
				result.push_back(Aurore::Protocol::PlayChunkColumn{
					.X = x, .Z = z, .MinimumY = -64, .Height = 384,
					.AirBlock = air, .Biome = plains,
				});
			}
		}

		Aurore::Protocol::PlayChunkColumn center{
			.X = 0, .Z = 0, .MinimumY = -64, .Height = 384,
			.AirBlock = air, .Biome = plains,
		};
		for (std::uint8_t z = 0; z < 5; ++z) {
			for (std::uint8_t x = 0; x < 5; ++x) {
				center.Blocks.push_back(Aurore::Protocol::PlayChunkBlock{
					.X = x, .Y = 61, .Z = z,
					.Block = ParseRequired("minecraft:stone"),
				});
				center.Blocks.push_back(Aurore::Protocol::PlayChunkBlock{
					.X = x, .Y = 62, .Z = z,
						.Block = ParseRequired("minecraft:dirt"),
				});
				center.Blocks.push_back(Aurore::Protocol::PlayChunkBlock{
					.X = x, .Y = 63, .Z = z,
					.Block = ParseRequired("minecraft:grass_block"),
				});
			}
		}
		result.push_back(std::move(center));
		return result;
	}

	TEST(PlayProtocolTests, UsesProtocol774FirstLightPacketIds) {
		EXPECT_EQ(Play::Clientbound::ChunkBatchFinishedPacketId, 0x0B);
		EXPECT_EQ(Play::Clientbound::ChunkBatchStartPacketId, 0x0C);
		EXPECT_EQ(Play::Clientbound::GameEventPacketId, 0x26);
		EXPECT_EQ(Play::Clientbound::LoginPacketId, 0x30);
		EXPECT_EQ(Play::Clientbound::PlayerPositionPacketId, 0x46);
		EXPECT_EQ(Play::Clientbound::SetChunkCacheCenterPacketId, 0x5C);
		EXPECT_EQ(Play::Clientbound::SetChunkCacheRadiusPacketId, 0x5D);
		EXPECT_EQ(Play::Serverbound::AcceptTeleportationPacketId, 0x00);
		EXPECT_EQ(Play::Serverbound::ChunkBatchReceivedPacketId, 0x0A);
		EXPECT_EQ(Play::Serverbound::ClientTickEndPacketId, 0x0C);
		EXPECT_EQ(Play::Serverbound::KeepAlivePacketId, 0x1B);
		EXPECT_EQ(Play::Serverbound::MovePlayerPositionPacketId, 0x1D);
		EXPECT_EQ(Play::Serverbound::MovePlayerPositionRotationPacketId, 0x1E);
		EXPECT_EQ(Play::Serverbound::MovePlayerRotationPacketId, 0x1F);
		EXPECT_EQ(Play::Serverbound::MovePlayerStatusOnlyPacketId, 0x20);
		EXPECT_EQ(Play::Serverbound::PlayerInputPacketId, 0x2A);
		EXPECT_EQ(Play::Serverbound::PlayerLoadedPacketId, 0x2B);
	}

	TEST(PlayProtocolTests, EncodesLoginBootstrapPacket) {
		const auto overworld = ParseRequired("minecraft:overworld");
		const auto frame = Play::Clientbound::Encode(Play::Clientbound::Login{
			.PlayerId = 7,
			.Hardcore = false,
			.Levels = { overworld },
			.MaximumPlayers = 20,
			.ChunkRadius = 2,
			.SimulationDistance = 2,
			.ReducedDebugInfo = false,
			.ShowDeathScreen = true,
			.LimitedCrafting = false,
			.SpawnInfo = Play::Clientbound::CommonSpawnInfo{
				.DimensionType = 0,
				.Dimension = overworld,
				.Seed = 0,
				.GameModeValue = Play::GameMode::Creative,
				.PreviousGameMode = -1,
				.IsDebug = false,
				.IsFlat = false,
				.HasLastDeathLocation = false,
				.PortalCooldown = 0,
				.SeaLevel = 63,
			},
			.EnforcesSecureChat = false,
		});

		EXPECT_EQ(frame.PacketId, Play::Clientbound::LoginPacketId);
		ByteReader reader(frame.Payload);
		ASSERT_EQ(*reader.ReadInt32(), 7);
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_EQ(*reader.ReadVarInt(), 1);
		ASSERT_EQ(*reader.ReadString(128), "minecraft:overworld");
		ASSERT_EQ(*reader.ReadVarInt(), 20);
		ASSERT_EQ(*reader.ReadVarInt(), 2);
		ASSERT_EQ(*reader.ReadVarInt(), 2);
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_TRUE(*reader.ReadBool());
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_EQ(*reader.ReadVarInt(), 0);
		ASSERT_EQ(*reader.ReadString(128), "minecraft:overworld");
		ASSERT_EQ(*reader.ReadInt64(), 0);
		ASSERT_EQ(*reader.ReadUnsignedByte(), 1);
		ASSERT_EQ(*reader.ReadUnsignedByte(), 0xFF);
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_FALSE(*reader.ReadBool());
		ASSERT_EQ(*reader.ReadVarInt(), 0);
		ASSERT_EQ(*reader.ReadVarInt(), 63);
		ASSERT_FALSE(*reader.ReadBool());
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, EncodesStartWaitingForLevelChunksEvent) {
		const auto frame = Play::Clientbound::Encode(
			Play::Clientbound::GameEvent{
				.Event = Play::Clientbound::GameEventType::StartWaitingForLevelChunks,
				.Value = 0.0F,
			});

		EXPECT_EQ(frame.PacketId, Play::Clientbound::GameEventPacketId);

		ByteReader reader(frame.Payload);
		ASSERT_EQ(*reader.ReadUnsignedByte(), 13u);
		EXPECT_FLOAT_EQ(*reader.ReadFloat(), 0.0F);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, EncodesChunkBatchFraming) {
		const auto start = Play::Clientbound::Encode(
			Play::Clientbound::ChunkBatchStart{});
		EXPECT_EQ(start.PacketId, Play::Clientbound::ChunkBatchStartPacketId);
		EXPECT_TRUE(start.Payload.empty());

		const auto finished = Play::Clientbound::Encode(
			Play::Clientbound::ChunkBatchFinished{
				.BatchSize = 1,
			});
		EXPECT_EQ(finished.PacketId, Play::Clientbound::ChunkBatchFinishedPacketId);

		ByteReader reader(finished.Payload);
		ASSERT_EQ(*reader.ReadVarInt(), 1);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, EncodesSetChunkCacheCenter) {
		const auto frame = Play::Clientbound::Encode(
			Play::Clientbound::SetChunkCacheCenter{
				.ChunkX = 0,
				.ChunkZ = 0,
			});

		EXPECT_EQ(frame.PacketId, Play::Clientbound::SetChunkCacheCenterPacketId);
		ByteReader reader(frame.Payload);
		ASSERT_EQ(*reader.ReadVarInt(), 0);
		ASSERT_EQ(*reader.ReadVarInt(), 0);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, EncodesSetChunkCacheRadius) {
		const auto frame = Play::Clientbound::Encode(
			Play::Clientbound::SetChunkCacheRadius{
				.Radius = 2,
			});

		EXPECT_EQ(
			frame.PacketId,
			Play::Clientbound::SetChunkCacheRadiusPacketId);

		ByteReader reader(frame.Payload);
		ASSERT_EQ(*reader.ReadVarInt(), 2);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, EncodesInitialPlayerPositionPacket) {
		const auto frame = Play::Clientbound::Encode(Play::Clientbound::PlayerPosition{
			.TeleportId = 1,
			.X = 0.5,
			.Y = 64.0,
			.Z = 0.5,
			.VelocityX = 0.0,
			.VelocityY = 0.0,
			.VelocityZ = 0.0,
			.Yaw = 0.0F,
			.Pitch = 0.0F,
			.RelativeFlags = 0,
		});

		EXPECT_EQ(frame.PacketId, Play::Clientbound::PlayerPositionPacketId);
		ByteReader reader(frame.Payload);
		ASSERT_EQ(*reader.ReadVarInt(), 1);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 0.5);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 64.0);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 0.5);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 0.0);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 0.0);
		EXPECT_DOUBLE_EQ(*reader.ReadDouble(), 0.0);
		EXPECT_FLOAT_EQ(*reader.ReadFloat(), 0.0F);
		EXPECT_FLOAT_EQ(*reader.ReadFloat(), 0.0F);
		EXPECT_EQ(*reader.ReadInt32(), 0);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(PlayProtocolTests, DecodesTeleportAcknowledgement) {
		ByteBuffer payload;
		payload.WriteVarInt(42);
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::AcceptTeleportationPacketId, payload));
		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::AcceptTeleportation>(*result));
		EXPECT_EQ(std::get<Play::Serverbound::AcceptTeleportation>(*result).TeleportId, 42);
	}

	TEST(PlayProtocolTests, DecodesChunkBatchReceived) {
		ByteBuffer payload;
		payload.WriteFloat(12.5F);

		const auto result = Play::Serverbound::Decode(
			MakeFrame(Play::Serverbound::ChunkBatchReceivedPacketId, payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::ChunkBatchReceived>(*result));
		EXPECT_FLOAT_EQ(
			std::get<Play::Serverbound::ChunkBatchReceived>(*result).ChunksPerTick,
			12.5F);
	}

	TEST(PlayProtocolTests, DecodesKeepAliveResponse) {
		ByteBuffer payload;
		payload.WriteInt64(-1234567890123LL);
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::KeepAlivePacketId, payload));
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
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::MovePlayerPositionPacketId, payload));
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
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::MovePlayerPositionRotationPacketId, payload));
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
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::MovePlayerRotationPacketId, payload));
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
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::MovePlayerStatusOnlyPacketId, payload));
		ASSERT_TRUE(result.has_value());
		const auto& packet = std::get<Play::Serverbound::MovePlayerStatusOnly>(*result);
		EXPECT_FALSE(packet.Flags.OnGround);
		EXPECT_FALSE(packet.Flags.HorizontalCollision);
	}

	TEST(PlayProtocolTests, DecodesPlayerInputFlags) {
		ByteBuffer payload;
		payload.WriteUnsignedByte(0x75);

		const auto result = Play::Serverbound::Decode(
			MakeFrame(Play::Serverbound::PlayerInputPacketId, payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::PlayerInput>(*result));

		const auto& flags = std::get<Play::Serverbound::PlayerInput>(*result).Flags;
		EXPECT_TRUE(flags.Forward);
		EXPECT_FALSE(flags.Backward);
		EXPECT_TRUE(flags.Left);
		EXPECT_FALSE(flags.Right);
		EXPECT_TRUE(flags.Jump);
		EXPECT_TRUE(flags.Shift);
		EXPECT_TRUE(flags.Sprint);
	}

	TEST(PlayProtocolTests, AcceptsEmptyTickEndAndPlayerLoaded) {
		const auto tick_end = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::ClientTickEndPacketId));
		const auto player_loaded = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::PlayerLoadedPacketId));
		ASSERT_TRUE(tick_end.has_value());
		ASSERT_TRUE(player_loaded.has_value());
		EXPECT_TRUE(std::holds_alternative<Play::Serverbound::ClientTickEnd>(*tick_end));
		EXPECT_TRUE(std::holds_alternative<Play::Serverbound::PlayerLoaded>(*player_loaded));
	}

	TEST(PlayProtocolTests, RejectsTrailingDataForEmptyPackets) {
		ByteBuffer payload;
		payload.WriteUnsignedByte(0xAA);
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::ClientTickEndPacketId, payload));
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::TrailingPacketData);
	}

	TEST(PlayProtocolTests, RejectsTruncatedMovementPacket) {
		ByteBuffer payload;
		payload.WriteDouble(1.0);
		const auto result = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::MovePlayerPositionPacketId, payload));
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::MalformedPacket);
	}

	TEST(PlayProtocolTests, RejectsUnknownPacketId) {
		const auto result = Play::Serverbound::Decode(MakeFrame(0x7F));
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ProtocolError::UnexpectedPacket);
	}

	TEST(PlayProtocolSessionTests, AdmissionEmitsWorldEntryBootstrapSequence) {
		auto fixture = EnterPlayWithAdmission();
		ASSERT_EQ(fixture.Admission.OutboundPackets.size(), 3u);
		EXPECT_EQ(GetPreparedFrame(fixture.Admission.OutboundPackets[0]).PacketId, Play::Clientbound::LoginPacketId);
		EXPECT_EQ(GetPreparedFrame(fixture.Admission.OutboundPackets[1]).PacketId, Play::Clientbound::GameEventPacketId);
		EXPECT_EQ(GetPreparedFrame(fixture.Admission.OutboundPackets[2]).PacketId, Play::Clientbound::PlayerPositionPacketId);
		EXPECT_EQ(fixture.Session.GetState(), ProtocolState::Play);
	}

	TEST(PlayProtocolSessionTests, AdmissionSendsNineLitChunkColumnsInOneBatch) {
		auto fixture = EnterPlayWithAdmission(MakeNineChunkColumns());
		const auto& packets = fixture.Admission.OutboundPackets;
		ASSERT_EQ(packets.size(), 16u);

		const std::array<std::int32_t, 6> prefix{
			Play::Clientbound::LoginPacketId,
			Play::Clientbound::GameEventPacketId,
			Play::Clientbound::PlayerPositionPacketId,
			Play::Clientbound::SetChunkCacheRadiusPacketId,
			Play::Clientbound::SetChunkCacheCenterPacketId,
			Play::Clientbound::ChunkBatchStartPacketId,
		};
		for (std::size_t i = 0; i < prefix.size(); ++i)
			EXPECT_EQ(GetPreparedFrame(packets[i]).PacketId, prefix[i]);

		{
			ByteReader radius(GetPreparedFrame(packets[3]).Payload);
			ASSERT_EQ(*radius.ReadVarInt(), 2);
			EXPECT_TRUE(radius.Empty());
		}
		{
			ByteReader center(GetPreparedFrame(packets[4]).Payload);
			ASSERT_EQ(*center.ReadVarInt(), 0);
			ASSERT_EQ(*center.ReadVarInt(), 0);
			EXPECT_TRUE(center.Empty());
		}

		std::set<std::pair<std::int32_t, std::int32_t>> positions;
		for (std::size_t i = 6; i < 15; ++i) {
			const auto& chunk = GetPreparedFrame(packets[i]);
			ASSERT_EQ(chunk.PacketId, Play::Clientbound::LevelChunkWithLightPacketId);
			ByteReader reader(chunk.Payload);
			const auto x = reader.ReadInt32();
			const auto z = reader.ReadInt32();
			ASSERT_TRUE(x.has_value());
			ASSERT_TRUE(z.has_value());
			EXPECT_GE(*x, -1);
			EXPECT_LE(*x, 1);
			EXPECT_GE(*z, -1);
			EXPECT_LE(*z, 1);
			EXPECT_TRUE(positions.emplace(*x, *z).second);
		}
		EXPECT_EQ(positions.size(), 9u);
		{
			ByteReader last_chunk(GetPreparedFrame(packets[14]).Payload);
			ASSERT_EQ(*last_chunk.ReadInt32(), 0);
			ASSERT_EQ(*last_chunk.ReadInt32(), 0);
		}
		const auto& finish = GetPreparedFrame(packets[15]);
		ASSERT_EQ(finish.PacketId, Play::Clientbound::ChunkBatchFinishedPacketId);
		ByteReader batch(finish.Payload);
		ASSERT_EQ(*batch.ReadVarInt(), 9);
		EXPECT_TRUE(batch.Empty());
		EXPECT_EQ(fixture.Session.GetState(), ProtocolState::Play);
	}

	TEST(PlayProtocolSessionTests, DispatchesKnownPacketAfterPlayAdmission) {
		auto session = EnterPlay();
		const auto result = session.HandlePacket(MakeFrame(Play::Serverbound::ClientTickEndPacketId));
		ASSERT_TRUE(result.has_value());
		EXPECT_TRUE(result->OutboundPackets.empty());
		EXPECT_TRUE(result->Requests.empty());
		EXPECT_FALSE(result->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Play);
	}

	TEST(PlayProtocolSessionTests, PlayerInputBecomesCoreObservationInsteadOfDisconnect) {
		auto session = EnterPlay();

		ByteBuffer payload;
		payload.WriteUnsignedByte(0x11);

		const auto result = session.HandlePacket(
			MakeFrame(Play::Serverbound::PlayerInputPacketId, payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_EQ(result->Requests.size(), 1u);
		ASSERT_TRUE(std::holds_alternative<PlayerInputRequest>(result->Requests.front()));

		const auto& request = std::get<PlayerInputRequest>(result->Requests.front());
		EXPECT_TRUE(request.Flags.Forward);
		EXPECT_TRUE(request.Flags.Jump);
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
