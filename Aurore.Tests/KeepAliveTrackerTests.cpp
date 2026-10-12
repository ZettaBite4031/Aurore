#include <Aurore/Core/KeepAliveTracker.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Play.hpp>
#include <Aurore/Protocol/ProtocolConnection.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <vector>
#include <cstdint>
#include <variant>

namespace Aurore::Tests {
	using namespace std::chrono_literals;
	using Aurore::Core::KeepAliveTracker;
	namespace Play = Aurore::Protocol::Packets::Play;

	TEST(KeepAliveTrackerTests, SendsAfterIntervalWithOnlyOneChallengeOutstanding) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		EXPECT_FALSE(tracker.ShouldSend(initial));
		EXPECT_FALSE(tracker.ShouldSend(initial + 9999ms));
		EXPECT_TRUE(tracker.ShouldSend(initial + 10s));
		tracker.MarkSent(12, initial + 10s);
		EXPECT_FALSE(tracker.ShouldSend(initial + 20s));
		ASSERT_TRUE(tracker.PendingId());
		EXPECT_EQ(*tracker.PendingId(), 12);
	}

	TEST(KeepAliveTrackerTests, MatchesExactChallengeAndMeasuresRtt) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		tracker.MarkSent(8123, initial + 10s);
		EXPECT_FALSE(tracker.Acknowledge(8122, initial + 11s));
		EXPECT_EQ(tracker.PendingId(), 8123);
		EXPECT_TRUE(tracker.Acknowledge(8123, initial + 11s + 75ms));
		EXPECT_FALSE(tracker.PendingId());
		ASSERT_TRUE(tracker.LastRtt());
		EXPECT_EQ(*tracker.LastRtt(), 1075ms);
		EXPECT_FALSE(tracker.Acknowledge(8123, initial + 12s));
	}

	TEST(KeepAliveTrackerTests, ReportsMissingAcknowledgementAtDeadline) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		tracker.MarkSent(44, initial + 10s);
		EXPECT_FALSE(tracker.IsExpired(initial + 24s + 999ms));
		EXPECT_TRUE(tracker.IsExpired(initial + 25s));
	}

	TEST(KeepAliveTrackerTests, RejectsRepliesThatArriveAfterDeadline) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		tracker.MarkSent(24, initial);
		EXPECT_FALSE(tracker.Acknowledge(24, initial + 15s));
		EXPECT_TRUE(tracker.IsExpired(initial + 15s));
		EXPECT_EQ(tracker.PendingId(), 24);
	}

	TEST(KeepAliveTrackerTests, NeverTimesOutBeforeFirstChallenge) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		EXPECT_FALSE(tracker.IsExpired(initial + 2min));
		EXPECT_FALSE(tracker.Acknowledge(1, initial + 2min));
	}

	TEST(KeepAliveTrackerTests, IgnoresClockReversalWithoutClearingChallenge) {
		const auto initial = KeepAliveTracker::Clock::now();
		KeepAliveTracker tracker(initial, { .Interval = 10s, .Deadline = 15s });
		tracker.MarkSent(5, initial);
		EXPECT_FALSE(tracker.IsExpired(initial - 1s));
		EXPECT_FALSE(tracker.Acknowledge(5, initial - 1s));
		EXPECT_EQ(tracker.PendingId(), 5);
	}

	TEST(KeepAlivePacketTests, EncodesClientboundChallengeAsNetworkInt64) {
		const auto frame = Play::Clientbound::Encode(Play::Clientbound::KeepAlive{ .Id = 0x0102030405060708LL });
		EXPECT_EQ(frame.PacketId, 0x2B);
		Aurore::Util::ByteReader reader(frame.Payload);
		const auto id = reader.ReadInt64();
		ASSERT_TRUE(id);
		EXPECT_EQ(*id, 0x0102030405060708LL);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(KeepAlivePacketTests, DecodesServerboundReplyWithoutIgnoringTrailingBytes) {
		Aurore::Util::ByteBuffer bytes;
		bytes.WriteInt64(123);
		Aurore::Protocol::PacketFrame frame{
			.PacketId = Play::Serverbound::KeepAlivePacketId,
			.Payload = std::vector<std::byte>(bytes.Bytes().begin(), bytes.Bytes().end()),
		};
		const auto decoded = Play::Serverbound::Decode(frame);
		ASSERT_TRUE(decoded);
		ASSERT_TRUE(std::holds_alternative<Play::Serverbound::KeepAlive>(*decoded));
		EXPECT_EQ(std::get<Play::Serverbound::KeepAlive>(*decoded).Id, 123);
		frame.Payload.push_back(std::byte{ 0 });
		EXPECT_FALSE(Play::Serverbound::Decode(frame));
	}

	TEST(KeepAlivePacketTests, RejectsServerInitiatedOutputBeforePlay) {
		Aurore::Protocol::ProtocolConnection connection;
		const auto frame = Play::Clientbound::Encode(Play::Clientbound::KeepAlive{ .Id = 1 });
		const auto result = connection.EncodePlayFrame(frame);
		ASSERT_FALSE(result);
		ASSERT_TRUE(std::holds_alternative<Aurore::Protocol::ProtocolError>(result.error()));
		EXPECT_EQ(std::get<Aurore::Protocol::ProtocolError>(result.error()), Aurore::Protocol::ProtocolError::UnexpectedPacket);
	}
}
