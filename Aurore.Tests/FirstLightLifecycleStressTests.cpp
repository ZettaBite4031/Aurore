#include "NetworkLoopbackClient.hpp"

#include <Aurore/Core/ClientService.hpp>
#include <Aurore/Core/RegistrySnapshotStore.hpp>
#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>
#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/World/WorldManager.hpp>
#include <Aurore/Network/NetworkManager.hpp>
#include <Aurore/Network/NetworkTypes.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <NetworkManagerTestAccess.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Aurore::Tests {
namespace {
    using namespace std::chrono_literals;
    namespace Config = Protocol::Packets::Configuration;
    namespace Login = Protocol::Packets::Login;
    namespace Play = Protocol::Packets::Play;
    using Network::Detail::NetworkManagerTestAccess;
    using Protocol::PacketFrame;
    using Util::ByteBuffer;
    using Util::ByteReader;

    constexpr auto Poll = 1ms;
    constexpr auto WaitLimit = 6s;
    constexpr std::size_t ReceiveBytes = 65536;

    bool ExtendedStress() {
        const char* value = std::getenv("AURORE_TEST_STRESS");
        return value != nullptr && std::string_view(value) == "1";
    }

    [[nodiscard]] ByteBuffer Frame(std::int32_t packet_id, const ByteBuffer& payload = {}) {
        return Protocol::EncodePacketFrame(packet_id, payload.Bytes());
    }

    [[nodiscard]] ByteBuffer Handshake(std::uint16_t port) {
        ByteBuffer payload;
        payload.WriteVarInt(774);
        payload.WriteString("localhost");
        payload.WriteUInt16(port);
        payload.WriteVarInt(static_cast<std::int32_t>(Protocol::HandshakeIntention::Login));
        return Frame(0x00, payload);
    }

    [[nodiscard]] ByteBuffer LoginStart(std::string_view name) {
        ByteBuffer payload;
        payload.WriteString(name);
        payload.WriteUuid({});
        return Frame(Login::Serverbound::StartPacketId, payload);
    }

    [[nodiscard]] ByteBuffer ClientInformation() {
        ByteBuffer payload;
        payload.WriteString("en_us");
        payload.WriteUnsignedByte(12);
        payload.WriteVarInt(static_cast<std::int32_t>(Config::ChatMode::Enabled));
        payload.WriteBool(true);
        payload.WriteUnsignedByte(0x7f);
        payload.WriteVarInt(static_cast<std::int32_t>(Config::MainHand::Right));
        payload.WriteBool(false);
        payload.WriteBool(true);
        payload.WriteVarInt(static_cast<std::int32_t>(Config::ParticleStatus::All));
        return Frame(Config::Serverbound::ClientInformationPacketId, payload);
    }

    [[nodiscard]] ByteBuffer TeleportReply(std::int32_t id) {
        ByteBuffer payload;
        payload.WriteVarInt(id);
        return Frame(Play::Serverbound::AcceptTeleportationPacketId, payload);
    }

    [[nodiscard]] ByteBuffer ChunkFeedback(float rate) {
        ByteBuffer payload;
        payload.WriteFloat(rate);
        return Frame(Play::Serverbound::ChunkBatchReceivedPacketId, payload);
    }

    [[nodiscard]] ByteBuffer KeepAliveReply(std::int64_t id) {
        ByteBuffer payload;
        payload.WriteInt64(id);
        return Frame(Play::Serverbound::KeepAlivePacketId, payload);
    }

    [[nodiscard]] ByteBuffer Movement(double x) {
        ByteBuffer payload;
        payload.WriteDouble(x);
        payload.WriteDouble(64.0);
        payload.WriteDouble(0.5);
        payload.WriteUnsignedByte(0x01);
        return Frame(Play::Serverbound::MovePlayerPositionPacketId, payload);
    }

    [[nodiscard]] Network::NetworkConfiguration TestNetworkConfiguration() {
        return Network::NetworkConfiguration{
            .Backend = Network::NetworkBackendType::Automatic,
            .BindAddress = "127.0.0.1",
            .Port = 0,
            .MaximumConnections = 64,
            .ReceiveBufferSize = 4096,
            .MaximumInboundBytesPerConnection = 2 * 1024 * 1024,
            .MaximumOutboundBytesPerConnection = 4 * 1024 * 1024,
            .MaximumCommandQueueEntries = 2048,
            .MaximumCommandQueueBytes = 32 * 1024 * 1024,
            .MaximumEventQueueEntries = 2048,
            .MaximumEventQueueBytes = 16 * 1024 * 1024,
            .MaximumTotalOutboundBytes = 64 * 1024 * 1024,
            .MaximumTotalInboundEventBytes = 64 * 1024 * 1024,
        };
    }

    struct SyntheticClient final {
        LoopbackClient Socket;
        Protocol::PacketFrameDecoder Frames;
        Network::ConnectionId Connection{};
        std::string Username;
        bool SawTeleport{ false };
        bool SawChunkBatch{ false };
        std::int32_t TeleportId{ 0 };
        std::int32_t ChunkCount{ 0 };

        [[nodiscard]] bool Send(const ByteBuffer& bytes) {
            return Socket.SendAll(bytes.Bytes());
        }
    };

    class FirstLightLifecycleStressTests : public ::testing::Test {
    protected:
        void SetUp() override {
            auto snapshot = Core::SyntheticRegistrySnapshotFactory::Build(
                Core::InitialSyntheticRegistryGeneration);
            ASSERT_TRUE(snapshot.has_value());
            ASSERT_TRUE(m_RegistrySnapshots.Publish(std::move(*snapshot)).has_value());

            auto vanilla = Util::ResourceLocation::Parse("minecraft:vanilla");
            auto manifest = Protocol::Protocol774RegistryManifest::Build();
            ASSERT_TRUE(vanilla.has_value());
            ASSERT_TRUE(manifest.has_value());
            Protocol::ConfigurationSequencePolicy policy;
            policy.EnabledFeatures.emplace_back(std::move(*vanilla));
            policy.RegistryManifest =
                std::make_shared<const Protocol::RegistryCompatibilityManifest>(std::move(*manifest));

            Core::ClientConfiguration clients;
            clients.IdleTimeout = 180s; // Test-only: permit immediate 60s synchronization clock jumps.
            clients.MaximumClients = 64;
            clients.MaximumPlayers = 20;
            ASSERT_TRUE(m_Service.Initialize(clients, std::move(policy),
                Config::DefaultLimits, Protocol::ServerStatus{}));
            ASSERT_TRUE(m_Worlds.Initialize());
            ASSERT_TRUE(m_Network.Initialize(TestNetworkConfiguration()).has_value());
            const auto started = m_Network.Start();
            ASSERT_TRUE(started.has_value());
            m_Endpoint = *started;
            ASSERT_NE(m_Endpoint.Port, 0);
        }

        void TearDown() override {
            m_Service.BeginShutdown();
            if (m_Network.IsInitialized()) m_Network.Shutdown();
            m_Service.CompleteShutdown();
            EXPECT_EQ(m_Service.GetConnectionCount(), 0u);
            EXPECT_EQ(m_Worlds.GetPlayerCount(), 0u);
            m_Worlds.Shutdown();
            const auto resources = NetworkManagerTestAccess::GetResourceSnapshot(m_Network);
            EXPECT_EQ(resources.TrackedConnections, 0u);
            EXPECT_EQ(resources.ActiveConnections, 0u);
            EXPECT_EQ(resources.TotalOutboundBytes, 0u);
            EXPECT_EQ(resources.TotalInboundEventBytes, 0u);
        }

        void Pump() {
            EXPECT_TRUE(m_Service.ProcessNetworkEvents());
        }

        template<class Predicate>
        [[nodiscard]] bool Until(Predicate&& predicate, std::chrono::milliseconds limit = WaitLimit) {
            const auto until = std::chrono::steady_clock::now() + limit;
            do {
                Pump();
                if (predicate()) return true;
                std::this_thread::sleep_for(Poll);
            } while (std::chrono::steady_clock::now() < until);
            Pump();
            return predicate();
        }

        [[nodiscard]] std::size_t Connections() const {
            return m_Service.GetConnectionCount();
        }

        [[nodiscard]] std::optional<PacketFrame> Next(SyntheticClient& client,
                                                       std::chrono::milliseconds limit = WaitLimit) {
            const auto until = std::chrono::steady_clock::now() + limit;
            do {
                Pump();
                auto next = client.Frames.TryDecode();
                if (!next) {
                    ADD_FAILURE() << "Synthetic client received malformed server framing";
                    return std::nullopt;
                }
                if (next->has_value()) return std::move(**next);
                auto bytes = client.Socket.ReceiveSome(ReceiveBytes, 0ms);
                if (bytes) {
                    if (bytes->empty()) return std::nullopt;
                    client.Frames.Append(*bytes);
                } else if (!client.Socket.IsLastErrorTransient()) {
                    return std::nullopt;
                }
                std::this_thread::sleep_for(Poll);
            } while (std::chrono::steady_clock::now() < until);
            return std::nullopt;
        }

        [[nodiscard]] bool ReadExpected(SyntheticClient& client, std::int32_t id) {
            const auto packet = Next(client);
            if (!packet || packet->PacketId != id) {
                ADD_FAILURE() << "Expected packet " << id << ", got "
                    << (packet ? std::to_string(packet->PacketId) : std::string("no packet"));
                return false;
            }
            return true;
        }

        [[nodiscard]] bool Open(SyntheticClient& client) {
            if (!client.Socket.IsReady()) return false;
            const auto baseline = m_Service.GetClientSnapshots();
            if (!client.Socket.Connect(m_Endpoint.Address, m_Endpoint.Port)) return false;
            const bool registered = Until([&] { return Connections() == baseline.size() + 1; });
            if (!registered) return false;
            const auto current = m_Service.GetClientSnapshots();
            for (const auto& entry : current) {
                const bool existed = std::any_of(baseline.begin(), baseline.end(),
                    [&](const auto& old) { return old.Connection == entry.Connection; });
                if (!existed) {
                    client.Connection = entry.Connection;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool EnterLogin(SyntheticClient& client) {
            if (!client.Send(Handshake(m_Endpoint.Port))) return false;
            return Until([&] {
                const auto* server_client = m_Service.FindClient(client.Connection);
                return server_client && server_client->GetProtocolState() == Protocol::ProtocolState::Login;
            });
        }

        [[nodiscard]] bool StartConfiguration(SyntheticClient& client, std::string_view name) {
            client.Username = std::string(name);
            if (!client.Send(LoginStart(name))) return false;
            if (!ReadExpected(client, Login::Clientbound::SuccessPacketId)) return false;

            // These packets can be delivered together after Login success,
            // matching the existing Configuration loopback regression test.
            if (!client.Send(Frame(Login::Serverbound::AcknowledgedPacketId))) return false;
            if (!client.Send(ClientInformation())) return false;
            if (!ReadExpected(client, Config::Clientbound::FeatureFlagsPacketId)) return false;
            return ReadExpected(client, Config::Clientbound::SelectKnownPacksPacketId);
        }

        [[nodiscard]] bool FinishConfiguration(SyntheticClient& client) {
            ByteBuffer empty_packs;
            empty_packs.WriteVarInt(0);
            if (!client.Send(Frame(Config::Serverbound::SelectKnownPacksPacketId, empty_packs))) return false;
            if (!ReadExpected(client, Config::Clientbound::RegistryDataPacketId)) return false;
            if (!ReadExpected(client, Config::Clientbound::RegistryDataPacketId)) return false;
            if (!ReadExpected(client, Config::Clientbound::TagsPacketId)) return false;
            if (!ReadExpected(client, Config::Clientbound::FinishConfigurationPacketId)) return false;
            if (!client.Send(Frame(Config::Serverbound::FinishConfigurationPacketId))) return false;
            return Until([&] {
                const auto* current = m_Service.FindClient(client.Connection);
                return current && current->GetProtocolState() == Protocol::ProtocolState::Play;
            });
        }

        [[nodiscard]] bool FinishPlayBootstrap(SyntheticClient& client, bool acknowledge = true) {
            // The initial chunk neighborhood consists of one batch of nine
            // chunks. Read the whole bootstrap to avoid filling the socket.
            for (int packets = 0; packets != 32; ++packets) {
                auto packet = Next(client);
                if (!packet) return false;
                if (packet->PacketId == Play::Clientbound::PlayerPositionPacketId) {
                    ByteReader payload(packet->Payload);
                    const auto id = payload.ReadVarInt();
                    if (!id) return false;
                    client.TeleportId = *id;
                    client.SawTeleport = true;
                }
                if (packet->PacketId == Play::Clientbound::ChunkBatchFinishedPacketId) {
                    ByteReader payload(packet->Payload);
                    const auto count = payload.ReadVarInt();
                    if (!count) return false;
                    client.ChunkCount = *count;
                    client.SawChunkBatch = true;
                    break;
                }
            }
            if (!client.SawTeleport || !client.SawChunkBatch || client.ChunkCount != 9) return false;
            if (!acknowledge) return true;
            if (!client.Send(TeleportReply(client.TeleportId))) return false;
            if (!client.Send(ChunkFeedback(10.0f))) return false;
            if (!client.Send(Frame(Play::Serverbound::PlayerLoadedPacketId))) return false;
            return Until([&] {
                const auto* current = m_Service.FindClient(client.Connection);
                const auto* world_player = m_Service.FindPlayer(client.Connection);
                return current && current->GetLifecycleState() == Core::ClientLifecycleState::Play
                    && world_player && world_player->IsLoaded();
            });
        }

        [[nodiscard]] bool Admit(SyntheticClient& client, std::string_view name,
                                 bool acknowledge = true) {
            return Open(client) && EnterLogin(client)
                && StartConfiguration(client, name)
                && FinishConfiguration(client)
                && FinishPlayBootstrap(client, acknowledge);
        }

        [[nodiscard]] std::optional<std::int64_t> ReadKeepAlive(SyntheticClient& client) {
            const auto packet = Next(client);
            if (!packet || packet->PacketId != Play::Clientbound::KeepAlivePacketId) {
                ADD_FAILURE() << "Expected clientbound Play keepalive";
                return std::nullopt;
            }
            ByteReader payload(packet->Payload);
            const auto id = payload.ReadInt64();
            if (!id || !payload.Empty()) return std::nullopt;
            return *id;
        }

        [[nodiscard]] Core::ClientConnection::TimePoint IssueKeepalives() {
            // Seed each per-connection scheduler in the past, then send the
            // challenge at real current time. Replies can therefore be
            // processed against the real monotonic clock without sleeps.
            const auto now = Core::ClientConnection::Clock::now();
            m_Service.ProcessTimeouts(now - 15s);
            m_Service.ProcessTimeouts(now);
            return now;
        }

        [[nodiscard]] bool CloseAndCollect(SyntheticClient& client, std::size_t expected) {
            client.Socket.Close();
            return Until([&] { return Connections() == expected; });
        }

        [[nodiscard]] bool NoActiveTransportResources() {
            // The backend can retain a completed close very briefly after
            // Core processes the close event. Keep draining until both agree.
            return Until([&] {
                const auto snapshot = NetworkManagerTestAccess::GetResourceSnapshot(m_Network);
                return Connections() == 0
                    && snapshot.TrackedConnections == 0
                    && snapshot.ActiveConnections == 0
                    && snapshot.TotalOutboundBytes == 0
                    && snapshot.TotalInboundEventBytes == 0;
            });
        }

        Network::NetworkManager m_Network;
        Core::RegistrySnapshotStore m_RegistrySnapshots;
        World::WorldManager m_Worlds;
        Core::ClientService m_Service{ m_Network, m_RegistrySnapshots, m_Worlds };
        Network::NetworkEndpoint m_Endpoint;
    };

    TEST_F(FirstLightLifecycleStressTests, RepeatedFullPlayAdmitAndDisconnectReleasesAllTransportState) {
        const int cycles = ExtendedStress() ? 100 : 12;
        for (int cycle = 0; cycle < cycles; ++cycle) {
            SCOPED_TRACE(::testing::Message() << "cycle=" << cycle);
            SyntheticClient client;
            ASSERT_TRUE(Admit(client, "RepeatUser"));
            ASSERT_TRUE(client.Send(Movement(0.5 + cycle)));
            ASSERT_TRUE(Until([&] { return Connections() == 1; }));
            ASSERT_TRUE(CloseAndCollect(client, 0));
            ASSERT_TRUE(NoActiveTransportResources());
            EXPECT_EQ(m_Worlds.GetPlayerCount(), 0u);
        }
    }

    TEST_F(FirstLightLifecycleStressTests, ConcurrentPlayClientsRemainIndependent) {
        const int participants = ExtendedStress() ? 16 : 8;
        std::vector<std::unique_ptr<SyntheticClient>> clients;
        for (int i = 0; i < participants; ++i) {
            SCOPED_TRACE(::testing::Message() << "join index=" << i);
            auto client = std::make_unique<SyntheticClient>();
            ASSERT_TRUE(Admit(*client, "User" + std::to_string(i)));
            ASSERT_EQ(Connections(), static_cast<std::size_t>(i + 1));
            clients.push_back(std::move(client));
        }
        for (int i = 0; i < participants; ++i) {
            ASSERT_TRUE(clients[i]->Send(Movement(3.0 + i)));
        }
        ASSERT_TRUE(Until([&] { return Connections() == clients.size(); }));
        EXPECT_EQ(m_Worlds.GetPlayerCount(), clients.size());
        for (int i = 0; i < participants; i += 2) {
            ASSERT_TRUE(CloseAndCollect(*clients[i], participants - (i / 2) - 1));
        }
        for (int i = 1; i < participants; i += 2) {
            const auto* current = m_Service.FindClient(clients[i]->Connection);
            ASSERT_NE(current, nullptr);
            EXPECT_EQ(current->GetLifecycleState(), Core::ClientLifecycleState::Play);
        }
        for (int i = 1; i < participants; i += 2) clients[i]->Socket.Close();
        EXPECT_TRUE(NoActiveTransportResources());
        EXPECT_EQ(m_Worlds.GetPlayerCount(), 0u);
    }

    TEST_F(FirstLightLifecycleStressTests, SameUsernameCanReconnectAfterFullSessionClosure) {
        SyntheticClient first;
        ASSERT_TRUE(Admit(first, "Returning"));
        const auto previous = first.Connection;
        ASSERT_TRUE(CloseAndCollect(first, 0));
        SyntheticClient second;
        ASSERT_TRUE(Admit(second, "Returning"));
        EXPECT_NE(previous, second.Connection);
        EXPECT_TRUE(CloseAndCollect(second, 0));
        EXPECT_TRUE(NoActiveTransportResources());
    }

    TEST_F(FirstLightLifecycleStressTests, DuplicateUsernameCannotEvictExistingPlayer) {
        SyntheticClient owner;
        ASSERT_TRUE(Admit(owner, "Occupied"));
        SyntheticClient duplicate;
        ASSERT_TRUE(Open(duplicate));
        ASSERT_TRUE(EnterLogin(duplicate));
        ASSERT_TRUE(duplicate.Send(LoginStart("Occupied")));
        const auto packet = Next(duplicate);
        ASSERT_TRUE(packet.has_value());
        EXPECT_EQ(packet->PacketId, Login::Clientbound::DisconnectPacketId);
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        const auto* existing = m_Service.FindClient(owner.Connection);
        ASSERT_NE(existing, nullptr);
        EXPECT_EQ(existing->GetProtocolState(), Protocol::ProtocolState::Play);
        ASSERT_TRUE(CloseAndCollect(owner, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, UnexpectedDisconnectAtEveryConnectionPhaseIsReclaimable) {
        for (int phase = 0; phase < 4; ++phase) {
            SCOPED_TRACE(::testing::Message() << "phase=" << phase);
            SyntheticClient client;
            ASSERT_TRUE(Open(client));
            if (phase >= 1) ASSERT_TRUE(EnterLogin(client));
            if (phase >= 2) ASSERT_TRUE(StartConfiguration(client, "PhaseUser"));
            if (phase >= 3) {
                ASSERT_TRUE(FinishConfiguration(client));
                ASSERT_TRUE(FinishPlayBootstrap(client));
            }
            ASSERT_TRUE(CloseAndCollect(client, 0));
            ASSERT_TRUE(NoActiveTransportResources());
        }
        // Every interrupted attempt must leave the identity reusable.
        SyntheticClient recovered;
        ASSERT_TRUE(Admit(recovered, "PhaseUser"));
        EXPECT_TRUE(CloseAndCollect(recovered, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, MalformedPlayPacketClosesOnlyOffendingClient) {
        SyntheticClient healthy;
        SyntheticClient attacker;
        ASSERT_TRUE(Admit(healthy, "Healthy"));
        ASSERT_TRUE(Admit(attacker, "Malformed"));
        ASSERT_TRUE(attacker.Send(Frame(0x7f)));
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        const auto* survivor = m_Service.FindClient(healthy.Connection);
        ASSERT_NE(survivor, nullptr);
        EXPECT_EQ(survivor->GetProtocolState(), Protocol::ProtocolState::Play);
        EXPECT_TRUE(healthy.Send(Movement(2.5)));
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, UnsolicitedKeepaliveReplyDisconnectsOnlySender) {
        SyntheticClient healthy;
        SyntheticClient attacker;
        ASSERT_TRUE(Admit(healthy, "Healthy"));
        ASSERT_TRUE(Admit(attacker, "Unsolicited"));
        ASSERT_TRUE(attacker.Send(KeepAliveReply(999)));
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, SplitFramesAreBufferedAndTruncatedFramesCleanUp) {
        SyntheticClient healthy;
        SyntheticClient slow;
        ASSERT_TRUE(Admit(healthy, "Healthy"));
        ASSERT_TRUE(Admit(slow, "Fragmented"));
        const auto movement = Movement(7.5);
        const auto bytes = movement.Bytes();
        ASSERT_GT(bytes.size(), 2u);
        ASSERT_TRUE(slow.Socket.SendAll(bytes.first(1)));
        // The pending partial frame must be retained, not rejected.
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(slow.Connection);
            return c && c->GetProtocolConnection().BufferedBytes() != 0;
        }));
        ASSERT_TRUE(slow.Socket.SendAll(bytes.subspan(1)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(slow.Connection);
            return c && c->GetProtocolConnection().BufferedBytes() == 0;
        }));
        ASSERT_TRUE(slow.Socket.SendAll(bytes.first(1)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(slow.Connection);
            return c && c->GetProtocolConnection().BufferedBytes() != 0;
        }));
        ASSERT_TRUE(CloseAndCollect(slow, 1));
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, InvalidSynchronisationFeedbackDoesNotDisconnectHealthyPlay) {
        SyntheticClient client;
        ASSERT_TRUE(Admit(client, "Feedback", false));
        const auto* admitted = m_Service.FindClient(client.Connection);
        ASSERT_NE(admitted, nullptr);
        const auto before = admitted->GetLastReceiveAt();
        ASSERT_TRUE(client.Send(TeleportReply(client.TeleportId + 1)));
        ASSERT_TRUE(client.Send(ChunkFeedback(-1.0f)));
        ASSERT_TRUE(client.Send(TeleportReply(client.TeleportId)));
        ASSERT_TRUE(client.Send(ChunkFeedback(8.0f)));
        ASSERT_TRUE(client.Send(Frame(Play::Serverbound::PlayerLoadedPacketId)));
        // Observe the final player-loaded packet via the last-receive clock.
		ASSERT_TRUE(Until([&] {
			const auto* player = m_Service.FindPlayer(client.Connection);
			return player != nullptr && player->IsLoaded();
		}));
        const auto* current = m_Service.FindClient(client.Connection);
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(current->GetProtocolState(), Protocol::ProtocolState::Play);
        const auto* player = m_Service.FindPlayer(client.Connection);
        ASSERT_NE(player, nullptr);
        EXPECT_TRUE(player->IsLoaded());
        EXPECT_TRUE(CloseAndCollect(client, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, NetworkShutdownWithPlayersConnectedDoesNotHang) {
        std::vector<std::unique_ptr<SyntheticClient>> clients;
        for (int i = 0; i < 4; ++i) {
            auto client = std::make_unique<SyntheticClient>();
            ASSERT_TRUE(Admit(*client, "Shutdown" + std::to_string(i)));
            clients.push_back(std::move(client));
        }
        m_Service.BeginShutdown();
        m_Network.Shutdown();
        m_Service.CompleteShutdown();
        EXPECT_EQ(m_Worlds.GetPlayerCount(), 0u);
        const auto resources = NetworkManagerTestAccess::GetResourceSnapshot(m_Network);
        EXPECT_EQ(resources.TrackedConnections, 0u);
        EXPECT_EQ(resources.ActiveConnections, 0u);
        EXPECT_EQ(resources.TotalOutboundBytes, 0u);
        EXPECT_EQ(resources.TotalInboundEventBytes, 0u);
        for (auto& client : clients) {
            EXPECT_TRUE(client->Socket.WaitForDisconnect(2s)) << client->Socket.GetLastError();
        }
    }


    TEST_F(FirstLightLifecycleStressTests, FragmentedHandshakeSurvivesSlowDelivery) {
        SyntheticClient client;
        ASSERT_TRUE(Open(client));
        const auto handshake = Handshake(m_Endpoint.Port);
        const auto bytes = handshake.Bytes();
        ASSERT_GT(bytes.size(), 2u);
        ASSERT_TRUE(client.Socket.SendAll(bytes.first(1)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(client.Connection);
            return c && c->GetProtocolConnection().BufferedBytes() > 0;
        }));
        ASSERT_TRUE(client.Socket.SendAll(bytes.subspan(1)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(client.Connection);
            return c && c->GetProtocolState() == Protocol::ProtocolState::Login;
        }));
        ASSERT_TRUE(StartConfiguration(client, "SlowStart"));
        ASSERT_TRUE(FinishConfiguration(client));
        ASSERT_TRUE(FinishPlayBootstrap(client));
        EXPECT_TRUE(CloseAndCollect(client, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, MalformedLoginDoesNotPoisonFutureAdmission) {
        SyntheticClient broken;
        ASSERT_TRUE(Open(broken));
        ASSERT_TRUE(EnterLogin(broken));
        // Login Start requires a username and UUID. This is deliberately empty.
        ASSERT_TRUE(broken.Send(Frame(Login::Serverbound::StartPacketId)));
        ASSERT_TRUE(Until([&] { return Connections() == 0; }));
        SyntheticClient recovered;
        ASSERT_TRUE(Admit(recovered, "AfterMalformed"));
        EXPECT_TRUE(CloseAndCollect(recovered, 0));
        EXPECT_TRUE(NoActiveTransportResources());
    }

    TEST_F(FirstLightLifecycleStressTests, ConfigurationDisconnectDoesNotDisturbPlaySessions) {
        SyntheticClient healthy;
        ASSERT_TRUE(Admit(healthy, "Healthy"));
        SyntheticClient interrupted;
        ASSERT_TRUE(Open(interrupted));
        ASSERT_TRUE(EnterLogin(interrupted));
        ASSERT_TRUE(StartConfiguration(interrupted, "Interrupt"));
        ASSERT_TRUE(CloseAndCollect(interrupted, 1));
        auto* existing = m_Service.FindClient(healthy.Connection);
        ASSERT_NE(existing, nullptr);
        EXPECT_EQ(existing->GetProtocolState(), Protocol::ProtocolState::Play);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }


    TEST_F(FirstLightLifecycleStressTests, GenuinePlayKeepaliveChallengeIsAnsweredAndReissued) {
        SyntheticClient client;
        ASSERT_TRUE(Admit(client, "KeepAliveOK"));
        const auto now = IssueKeepalives();
        const auto id = ReadKeepAlive(client);
        ASSERT_TRUE(id.has_value());
        const auto* before = m_Service.FindClient(client.Connection);
        ASSERT_NE(before, nullptr);
        const auto received_before = before->GetLastReceiveAt();
        ASSERT_TRUE(client.Send(KeepAliveReply(*id)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(client.Connection);
            return c && c->GetLastReceiveAt() > received_before;
        }));
        // A second keepalive demonstrates that the first outstanding
        // challenge was cleared, rather than merely ignored.
        m_Service.ProcessTimeouts(now + 16s);
        const auto next_id = ReadKeepAlive(client);
        ASSERT_TRUE(next_id.has_value());
        EXPECT_NE(*next_id, *id);
        EXPECT_TRUE(CloseAndCollect(client, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, MissingKeepaliveDisconnectsOnlyUnresponsiveClient) {
        SyntheticClient healthy;
        SyntheticClient silent;
        ASSERT_TRUE(Admit(healthy, "Alive"));
        ASSERT_TRUE(Admit(silent, "Silent"));
        const auto now = IssueKeepalives();
        const auto healthy_id = ReadKeepAlive(healthy);
        const auto silent_id = ReadKeepAlive(silent);
        ASSERT_TRUE(healthy_id.has_value());
        ASSERT_TRUE(silent_id.has_value());
        ASSERT_TRUE(healthy.Send(KeepAliveReply(*healthy_id)));
        const auto before = m_Service.FindClient(healthy.Connection)->GetLastReceiveAt();
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(healthy.Connection);
            return c && c->GetLastReceiveAt() > before;
        }));
        // The deadline is derived from the fixture's 180s idle timeout:
        // interval=15s, keepalive response deadline=20s.
        m_Service.ProcessTimeouts(now + 20s);
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        EXPECT_EQ(m_Worlds.GetPlayerCount(), 1u);
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, IncorrectPendingKeepaliveIdCannotEvictPeer) {
        SyntheticClient healthy;
        SyntheticClient wrong;
        ASSERT_TRUE(Admit(healthy, "Alive"));
        ASSERT_TRUE(Admit(wrong, "WrongId"));
        (void)IssueKeepalives();
        const auto healthy_id = ReadKeepAlive(healthy);
        const auto wrong_id = ReadKeepAlive(wrong);
        ASSERT_TRUE(healthy_id.has_value());
        ASSERT_TRUE(wrong_id.has_value());
        ASSERT_TRUE(wrong.Send(KeepAliveReply(*wrong_id + 1234)));
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, MissingTeleportAcknowledgementTimesOutOnlyThatSession) {
        SyntheticClient healthy;
        SyntheticClient stalled;
        ASSERT_TRUE(Admit(healthy, "Alive"));
        ASSERT_TRUE(Admit(stalled, "NoTeleport", false));
        // ClientService checks synchronization deadlines before keepalives.
        // Test config's longer idle timeout prevents unrelated idle expiry.
        const auto now = Core::ClientConnection::Clock::now();
        m_Service.ProcessTimeouts(now + 61s);
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        EXPECT_EQ(m_Worlds.GetPlayerCount(), 1u);
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

    TEST_F(FirstLightLifecycleStressTests, MissingChunkFeedbackTimesOutAfterTeleportIsAcknowledged) {
        SyntheticClient healthy;
        SyntheticClient stalled;
        ASSERT_TRUE(Admit(healthy, "Alive"));
        ASSERT_TRUE(Admit(stalled, "NoChunks", false));
        const auto* before = m_Service.FindClient(stalled.Connection);
        ASSERT_NE(before, nullptr);
        const auto received_before = before->GetLastReceiveAt();
        ASSERT_TRUE(stalled.Send(TeleportReply(stalled.TeleportId)));
        ASSERT_TRUE(Until([&] {
            const auto* c = m_Service.FindClient(stalled.Connection);
            return c && c->GetLastReceiveAt() > received_before;
        }));
        const auto now = Core::ClientConnection::Clock::now();
        m_Service.ProcessTimeouts(now + 61s);
        ASSERT_TRUE(Until([&] { return Connections() == 1; }));
        ASSERT_NE(m_Service.FindClient(healthy.Connection), nullptr);
        EXPECT_TRUE(CloseAndCollect(healthy, 0));
    }

} // namespace
} // namespace Aurore::Tests
