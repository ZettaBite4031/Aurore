#include <Aurore/Core/ClientLifecycle.hpp>
#include <Aurore/Core/KeepAliveTracker.hpp>
#include <Aurore/Core/PlayerSynchronizationTracker.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Aurore::Tests {
namespace {
    using namespace std::chrono_literals;
    using Core::ClientCloseCause;
    using Core::ClientConfiguration;
    using Core::ConnectionManager;
    using Core::KeepAliveTracker;
    using Core::PlayerSynchronizationTracker;
    using Network::ConnectionId;
    using Network::NetworkEndpoint;

    [[nodiscard]] Protocol::ServerStatus Status() {
        return { .VersionName = "Aurore", .ProtocolVersion = 774,
                 .MaximumPlayers = 20, .OnlinePlayers = 0, .Description = "Tests" };
    }

    [[nodiscard]] NetworkEndpoint Local() { return { "127.0.0.1", 25565 }; }
    [[nodiscard]] NetworkEndpoint Remote(std::uint16_t port) { return { "127.0.0.1", port }; }

    [[nodiscard]] Util::ByteBuffer Handshake(int intention) {
        Util::ByteBuffer body;
        body.WriteVarInt(774);
        body.WriteString("localhost");
        body.WriteUInt16(25565);
        body.WriteVarInt(intention);
        return Protocol::EncodePacketFrame(0, body.Bytes());
    }

    TEST(LifecycleFailureDecisionTests, MultipleHandshakeDeadlinesExpireIndependently) {
        ClientConfiguration config;
        config.HandshakeTimeout = 3s;
        ConnectionManager manager(config);
        const auto t0 = ConnectionManager::Clock::now();
        ASSERT_TRUE(manager.Open(ConnectionId{ 1 }, Local(), Remote(50001), Status(), t0));
        ASSERT_TRUE(manager.Open(ConnectionId{ 2 }, Local(), Remote(50002), Status(), t0 + 2s));
        auto expired = manager.CollectTimeouts(t0 + 3s);
        ASSERT_EQ(expired.size(), 1u);
        EXPECT_EQ(expired.front().Connection, (ConnectionId{ 1 }));
        EXPECT_EQ(expired.front().Cause, ClientCloseCause::HandshakeTimeout);
        expired = manager.CollectTimeouts(t0 + 5s);
        ASSERT_EQ(expired.size(), 2u);
    }

    TEST(LifecycleFailureDecisionTests, LoginDeadlineIsNotResetByFurtherLoginBytes) {
        ClientConfiguration config;
        config.LoginTimeout = 3s;
        ConnectionManager manager(config);
        const auto t0 = ConnectionManager::Clock::now();
        auto open = manager.Open(ConnectionId{ 3 }, Local(), Remote(50003), Status(), t0);
        ASSERT_TRUE(open.has_value());
        auto& client = open->get();
        const auto handshake = Handshake(2);
        const auto result = client.Receive(handshake.Bytes(), t0 + 1s);
        ASSERT_FALSE(result.HasError());
        EXPECT_EQ(client.GetLifecycleState(), Core::ClientLifecycleState::Login);
        // An incomplete frame updates last activity but not the Login state deadline.
        const std::byte partial{ 0x01 };
        const auto incomplete = client.Receive(std::span{ &partial, 1 }, t0 + 3s);
        EXPECT_FALSE(incomplete.HasError());
        const auto timed_out = client.CheckTimeout(config, t0 + 4s);
        ASSERT_TRUE(timed_out);
        EXPECT_EQ(timed_out->Cause, ClientCloseCause::LoginTimeout);
    }

    TEST(LifecycleFailureDecisionTests, StatusIdleDeadlineAndClosingSuppression) {
        ClientConfiguration config;
        config.IdleTimeout = 3s;
        ConnectionManager manager(config);
        const auto t0 = ConnectionManager::Clock::now();
        auto opened = manager.Open(ConnectionId{ 4 }, Local(), Remote(50004), Status(), t0);
        ASSERT_TRUE(opened);
        auto& client = opened->get();
        const auto handshake = Handshake(1);
        ASSERT_FALSE(client.Receive(handshake.Bytes(), t0 + 1s).HasError());
        EXPECT_EQ(client.GetLifecycleState(), Core::ClientLifecycleState::Status);
        EXPECT_FALSE(client.CheckTimeout(config, t0 + 3s));
        const auto expired = client.CheckTimeout(config, t0 + 4s);
        ASSERT_TRUE(expired);
        EXPECT_EQ(expired->Cause, ClientCloseCause::IdleTimeout);
        client.MarkCloseRequested(ClientCloseCause::ProtocolRequested);
        EXPECT_FALSE(client.CheckTimeout(config, t0 + 100s));
    }

    TEST(LifecycleFailureDecisionTests, ConnectionCapacityRecoversAfterRemoval) {
        ClientConfiguration config;
        config.MaximumClients = 2;
        ConnectionManager manager(config);
        ASSERT_TRUE(manager.Open(ConnectionId{ 1 }, Local(), Remote(50001), Status()));
        ASSERT_TRUE(manager.Open(ConnectionId{ 2 }, Local(), Remote(50002), Status()));
        const auto refused = manager.Open(ConnectionId{ 3 }, Local(), Remote(50003), Status());
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error(), Core::ClientError::CapacityExceeded);
        EXPECT_TRUE(manager.Remove(ConnectionId{ 1 }));
        EXPECT_TRUE(manager.Open(ConnectionId{ 3 }, Local(), Remote(50003), Status()));
        EXPECT_EQ(manager.Size(), 2u);
    }

    TEST(LifecycleFailureDecisionTests, UnresponsiveAndResponsiveKeepalivesRemainIndependent) {
        const auto t0 = KeepAliveTracker::Clock::now();
        KeepAliveTracker missing(t0, { .Interval = 10s, .Deadline = 15s });
        KeepAliveTracker responsive(t0, { .Interval = 10s, .Deadline = 15s });
        missing.MarkSent(8, t0 + 10s);
        responsive.MarkSent(9, t0 + 10s);
        ASSERT_TRUE(responsive.Acknowledge(9, t0 + 11s));
        EXPECT_FALSE(responsive.IsExpired(t0 + 26s));
        EXPECT_TRUE(missing.IsExpired(t0 + 25s));
        EXPECT_TRUE(responsive.ShouldSend(t0 + 26s));
    }

    TEST(LifecycleFailureDecisionTests, SynchronizationDeadlinesArePerSessionAndPerOperation) {
        const auto t0 = PlayerSynchronizationTracker::Clock::now();
        PlayerSynchronizationTracker stalled;
        PlayerSynchronizationTracker ready;
        stalled.ExpectTeleport(1, t0);
        stalled.ExpectChunkBatch(t0);
        ready.ExpectTeleport(1, t0);
        ready.ExpectChunkBatch(t0);
        ASSERT_EQ(ready.AcknowledgeTeleport(1, t0 + 100ms), PlayerSynchronizationTracker::Result::Accepted);
        ASSERT_EQ(ready.ReceiveChunkBatchFeedback(8.0f, t0 + 200ms), PlayerSynchronizationTracker::Result::Accepted);
        EXPECT_FALSE(ready.Expired(t0 + 90s));
        EXPECT_EQ(stalled.Expired(t0 + 60s), PlayerSynchronizationTracker::Expiration::Teleport);
    }
}
}
