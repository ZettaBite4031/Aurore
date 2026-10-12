#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>
#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Protocol/ProtocolDiagnostics.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>
#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/UUID.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		namespace Play = Aurore::Protocol::Packets::Play;
		namespace Login = Aurore::Protocol::Packets::Login;
		namespace Configuration = Aurore::Protocol::Packets::Configuration;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ProtocolSession;
		using Aurore::Protocol::ProtocolState;
		using Aurore::Util::ByteBuffer;

		[[nodiscard]] PacketFrame MakeFrame(std::int32_t id, const ByteBuffer& data = {}) {
			return PacketFrame{
				.PacketId = id,
				.Payload = std::vector<std::byte>(data.Bytes().begin(), data.Bytes().end()),
			};
		}

		[[nodiscard]] Aurore::Util::ResourceLocation Location(std::string_view id) {
			auto value = Aurore::Util::ResourceLocation::Parse(id);
			if (!value) throw std::runtime_error("Invalid resource identifier in test");
			return std::move(*value);
		}

		template<typename Request>
		[[nodiscard]] Request RequiredRequest(const Aurore::Protocol::ProtocolAction& action) {
			if (action.Requests.size() != 1 || !std::holds_alternative<Request>(action.Requests.front()))
				throw std::runtime_error("Expected a single Core request");
			return std::get<Request>(action.Requests.front());
		}

		[[nodiscard]] ProtocolSession EnterPlay() {
			using namespace Aurore::Protocol;
			ProtocolSession session;
			ByteBuffer handshake;
			handshake.WriteVarInt(774);
			handshake.WriteString("localhost");
			handshake.WriteUInt16(25565);
			handshake.WriteVarInt(static_cast<std::int32_t>(HandshakeIntention::Login));
			if (!session.HandlePacket(MakeFrame(0x00, handshake)))
				throw std::runtime_error("Handshake failed");

			ByteBuffer login_start;
			login_start.WriteString("PlayerOne");
			login_start.WriteUuid({});
			const auto login = session.HandlePacket(MakeFrame(Login::Serverbound::StartPacketId, login_start));
			if (!login) throw std::runtime_error("Login failed");
			const auto login_id = RequiredRequest<LoginStartRequest>(*login).Id;
			if (!session.ResolveLogin(LoginResolution{ LoginAcceptedResolution{
				.Id = login_id,
				.ProfileId = Aurore::Util::Uuid::FromOfflinePlayerName("PlayerOne"),
				.Username = "PlayerOne",
				.Properties = {},
			} })) throw std::runtime_error("Login resolution failed");

			const auto accepted = session.HandlePacket(MakeFrame(Login::Serverbound::AcknowledgedPacketId));
			if (!accepted) throw std::runtime_error("Login acknowledgement failed");
			const auto config_id = RequiredRequest<ConfigurationStartRequest>(*accepted).Id;

			auto manifest = Protocol774RegistryManifest::Build();
			if (!manifest) throw std::runtime_error("Registry manifest failed");
			auto snapshot = Aurore::Core::SyntheticRegistrySnapshotFactory::Build(1);
			if (!snapshot) throw std::runtime_error("Registry snapshot failed");
			auto plan = ConfigurationSequenceBuilder::Build(
				std::move(*snapshot), ConfigurationSequencePolicy{
					.EnabledFeatures = { Location("minecraft:vanilla") },
					.KnownPacks = {},
					.RegistryManifest = std::make_shared<const RegistryCompatibilityManifest>(std::move(*manifest)),
				});
			if (!plan) throw std::runtime_error("Configuration plan failed");
			if (!session.ResolveConfiguration(ConfigurationResolution{ ConfigurationReadyResolution{
				.Id = config_id,
				.Plan = std::move(*plan),
			} })) throw std::runtime_error("Configuration bootstrap failed");

			ByteBuffer packs;
			packs.WriteVarInt(0);
			if (!session.HandlePacket(MakeFrame(Configuration::Serverbound::SelectKnownPacksPacketId, packs)))
				throw std::runtime_error("Known Packs selection failed");
			const auto finish = session.HandlePacket(MakeFrame(Configuration::Serverbound::FinishConfigurationPacketId));
			if (!finish) throw std::runtime_error("Configuration finish failed");
			const auto completion = RequiredRequest<ConfigurationCompleteRequest>(*finish);
			if (!session.ResolveConfiguration(ConfigurationResolution{ ConfigurationAcceptedResolution{
				.Id = completion.Id,
				.Admission = PlayAdmission{},
			} })) throw std::runtime_error("Play admission failed");
			if (session.GetState() != ProtocolState::Play)
				throw std::runtime_error("Session did not enter Play");
			return session;
		}
	}

	TEST(FirstLightInputTests, NamesAreVersionAndDirectionSpecific) {
		using Aurore::Protocol::PacketDirection;
		using Aurore::Protocol::FindPacketName;
		EXPECT_EQ(FindPacketName(ProtocolState::Play, PacketDirection::Serverbound, 0x29),
			(std::optional<std::string_view>{ "minecraft:player_command" }));
		EXPECT_EQ(FindPacketName(ProtocolState::Play, PacketDirection::Clientbound, 0x04),
			(std::optional<std::string_view>{ "minecraft:block_changed_ack" }));
		EXPECT_EQ(FindPacketName(ProtocolState::Login, PacketDirection::Serverbound, 0x00),
            (std::optional<std::string_view>{ "minecraft:hello" }));
        EXPECT_EQ(FindPacketName(ProtocolState::Configuration, PacketDirection::Clientbound, 0x07),
            (std::optional<std::string_view>{ "minecraft:registry_data" }));
        EXPECT_EQ(FindPacketName(ProtocolState::Handshake, PacketDirection::Serverbound, 0x00),
            (std::optional<std::string_view>{ "minecraft:intention" }));
		EXPECT_FALSE(FindPacketName(ProtocolState::Login, PacketDirection::Serverbound, 0x29));
		EXPECT_FALSE(FindPacketName(ProtocolState::Play, PacketDirection::Serverbound, 0x7F));
		EXPECT_EQ(Aurore::Protocol::GetProtocolErrorName(Aurore::Protocol::ProtocolError::PlayBootstrapFailed),
			"PlayBootstrapFailed");
	}


	TEST(FirstLightInputTests, PlayClientInformationUpdatesSessionPreferences) {
		auto session = EnterPlay();

		ByteBuffer initial;
		initial.WriteString("en_us");
		initial.WriteUnsignedByte(8); // View distance (signed byte)
		initial.WriteVarInt(0); // Chat enabled
		initial.WriteBool(true); // Chat colors
		initial.WriteUnsignedByte(0x7F); // Visible skin parts
		initial.WriteVarInt(1); // Right hand
		initial.WriteBool(false); // Text filtering
		initial.WriteBool(true); // Server listing
		initial.WriteVarInt(0); // All particles

		const auto decoded = Play::Serverbound::Decode(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, initial));
		ASSERT_TRUE(decoded);
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::ClientInformation>(*decoded));
		EXPECT_EQ(std::get<Play::Serverbound::ClientInformation>(*decoded).Locale, "en_us");

		const auto first = session.HandlePacket(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, initial));
		ASSERT_TRUE(first);
		EXPECT_TRUE(first->Requests.empty());
		EXPECT_TRUE(first->OutboundPackets.empty());
		ASSERT_TRUE(session.GetClientInformation());
		EXPECT_EQ(session.GetClientInformation()->Locale, "en_us");
		EXPECT_EQ(session.GetClientInformation()->ViewDistance, 8);

		ByteBuffer updated;
		updated.WriteString("fr_fr");
		updated.WriteUnsignedByte(12);
		updated.WriteVarInt(1); // Commands-only chat
		updated.WriteBool(false);
		updated.WriteUnsignedByte(0x3F);
		updated.WriteVarInt(0); // Left hand
		updated.WriteBool(true);
		updated.WriteBool(false);
		updated.WriteVarInt(2); // Minimal particles

		const auto second = session.HandlePacket(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, updated));
		ASSERT_TRUE(second);
		EXPECT_FALSE(second->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Play);
		ASSERT_TRUE(session.GetClientInformation());
		EXPECT_EQ(session.GetClientInformation()->Locale, "fr_fr");
		EXPECT_EQ(session.GetClientInformation()->ViewDistance, 12);
		EXPECT_EQ(session.GetClientInformation()->ChatModeValue, Configuration::ChatMode::CommandsOnly);
		EXPECT_EQ(session.GetClientInformation()->MainHandValue, Configuration::MainHand::Left);
		EXPECT_EQ(session.GetClientInformation()->ParticleStatusValue, Configuration::ParticleStatus::Minimal);
	}

	TEST(FirstLightInputTests, MalformedPlayClientInformationDoesNotReplacePreferences) {
		auto session = EnterPlay();
		ByteBuffer valid;
		valid.WriteString("en_us");
		valid.WriteUnsignedByte(8);
		valid.WriteVarInt(0);
		valid.WriteBool(true);
		valid.WriteUnsignedByte(0x7F);
		valid.WriteVarInt(1);
		valid.WriteBool(false);
		valid.WriteBool(true);
		valid.WriteVarInt(0);
		ASSERT_TRUE(session.HandlePacket(MakeFrame(Play::Serverbound::ClientInformationPacketId, valid)));

		ByteBuffer malformed;
		malformed.WriteString("fr_fr");
		malformed.WriteUnsignedByte(8);
		malformed.WriteVarInt(99); // Invalid chat mode
		malformed.WriteBool(true);
		malformed.WriteUnsignedByte(0x7F);
		malformed.WriteVarInt(1);
		malformed.WriteBool(false);
		malformed.WriteBool(true);
		malformed.WriteVarInt(0);

		const auto invalid = session.HandlePacket(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, malformed));
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.error(), Aurore::Protocol::ProtocolError::MalformedPacket);
		ASSERT_TRUE(session.GetClientInformation());
		EXPECT_EQ(session.GetClientInformation()->Locale, "en_us");

		ByteBuffer trailing = valid;
		trailing.WriteUnsignedByte(0x00);
		const auto extra = Play::Serverbound::Decode(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, trailing));
		ASSERT_FALSE(extra);
		EXPECT_EQ(extra.error(), Aurore::Protocol::ProtocolError::TrailingPacketData);

		ByteBuffer truncated;
		truncated.WriteString("en_us");
		const auto incomplete = Play::Serverbound::Decode(
			MakeFrame(Play::Serverbound::ClientInformationPacketId, truncated));
		ASSERT_FALSE(incomplete);
		EXPECT_EQ(incomplete.error(), Aurore::Protocol::ProtocolError::MalformedPacket);
	}

	TEST(FirstLightInputTests, DecodesSprintCommandAndRejectsTrailingBytes) {
		ByteBuffer payload;
		payload.WriteVarInt(1);
		payload.WriteVarInt(1); // START_SPRINTING in 1.21.11
		payload.WriteVarInt(0);
		const auto decoded = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::PlayerCommandPacketId, payload));
		ASSERT_TRUE(decoded);
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::PlayerCommand>(*decoded));
		EXPECT_EQ(std::get<Play::Serverbound::PlayerCommand>(*decoded).ActionId, 1);
		payload.WriteUnsignedByte(0);
		const auto invalid = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::PlayerCommandPacketId, payload));
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.error(), Aurore::Protocol::ProtocolError::TrailingPacketData);
	}

	TEST(FirstLightInputTests, HandlesHotbarAndInventoryCloseWithoutMutation) {
		ByteBuffer hotbar;
		hotbar.WriteInt16(8);
		const auto selected = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::SetCarriedItemPacketId, hotbar));
		ASSERT_TRUE(selected);
		EXPECT_EQ(std::get<Play::Serverbound::SetCarriedItem>(*selected).Slot, 8);

		ByteBuffer close;
		close.WriteVarInt(0);
		const auto closed = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::ContainerClosePacketId, close));
		ASSERT_TRUE(closed);
		EXPECT_EQ(std::get<Play::Serverbound::ContainerClose>(*closed).ContainerId, 0);

		ByteBuffer bad;
		bad.WriteInt16(9);
		EXPECT_FALSE(Play::Serverbound::Decode(MakeFrame(Play::Serverbound::SetCarriedItemPacketId, bad)));
	}

	TEST(FirstLightInputTests, OpaqueCreativeSlotUpdatesAreBounded) {
		ByteBuffer item;
		item.WriteInt16(36);
		item.WriteVarInt(0);
		const auto decoded = Play::Serverbound::Decode(MakeFrame(Play::Serverbound::SetCreativeModeSlotPacketId, item));
		ASSERT_TRUE(decoded);
		EXPECT_TRUE(std::holds_alternative<Play::Serverbound::DeferredInteraction>(*decoded));

		PacketFrame oversized{ .PacketId = Play::Serverbound::SetCreativeModeSlotPacketId,
			.Payload = std::vector<std::byte>(64 * 1024 + 1) };
		EXPECT_FALSE(Play::Serverbound::Decode(oversized));
	}

	TEST(FirstLightInputTests, SequenceBearingUseItemAcknowledgesWithoutGameplayRequest) {
		auto session = EnterPlay();
		ByteBuffer payload;
		payload.WriteVarInt(0);   // Main hand
		payload.WriteVarInt(17);  // Client prediction sequence
		payload.WriteFloat(0.0F); // Yaw
		payload.WriteFloat(0.0F); // Pitch
		const auto result = session.HandlePacket(MakeFrame(Play::Serverbound::UseItemPacketId, payload));
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->Requests.empty());
		ASSERT_EQ(result->OutboundPackets.size(), 1u);
		ASSERT_TRUE(std::holds_alternative<Aurore::Protocol::PreparedPacketFrame>(result->OutboundPackets[0]));
		const auto& frame = std::get<Aurore::Protocol::PreparedPacketFrame>(result->OutboundPackets[0]).Frame;
		EXPECT_EQ(frame.PacketId, Play::Clientbound::BlockChangedAckPacketId);
		Aurore::Util::ByteReader reader(frame.Payload);
		const auto sequence = reader.ReadVarInt();
		ASSERT_TRUE(sequence);
		EXPECT_EQ(*sequence, 17);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(FirstLightInputTests, OrdinaryDeferredCommandsDoNotDisconnectOrCreateCoreRequests) {
		auto session = EnterPlay();
		ByteBuffer command;
		command.WriteVarInt(1);
		command.WriteVarInt(1); // Start sprinting (protocol 774)
		command.WriteVarInt(0);
		auto sprint = session.HandlePacket(MakeFrame(Play::Serverbound::PlayerCommandPacketId, command));
		ASSERT_TRUE(sprint);
		EXPECT_TRUE(sprint->Requests.empty());
		EXPECT_FALSE(sprint->Disconnect);
		EXPECT_EQ(session.GetState(), ProtocolState::Play);

		ByteBuffer close;
		close.WriteVarInt(0);
		auto inventory = session.HandlePacket(MakeFrame(Play::Serverbound::ContainerClosePacketId, close));
		ASSERT_TRUE(inventory);
		EXPECT_TRUE(inventory->OutboundPackets.empty());
		EXPECT_TRUE(inventory->Requests.empty());
	}
}

