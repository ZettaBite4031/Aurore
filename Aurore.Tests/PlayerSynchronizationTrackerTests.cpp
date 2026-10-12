#include <Aurore/Core/PlayerSynchronizationTracker.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <limits>

namespace Aurore::Tests {
	using namespace std::chrono_literals;
	using Aurore::Core::PlayerSynchronizationTracker;
	using Result = PlayerSynchronizationTracker::Result;
	using Expiration = PlayerSynchronizationTracker::Expiration;

	TEST(PlayerSynchronizationTrackerTests, InitialTeleportAcknowledgementClearsOnlyMatchingOperation) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		tracker.ExpectTeleport(1, t);
		EXPECT_EQ(tracker.AcknowledgeTeleport(2, t + 1s), Result::Unexpected);
		EXPECT_EQ(tracker.PendingTeleportId(), 1);
		EXPECT_EQ(tracker.AcknowledgeTeleport(1, t + 2s), Result::Accepted);
		EXPECT_FALSE(tracker.PendingTeleportId());
		EXPECT_EQ(tracker.LastAcknowledgedTeleportId(), 1);
		EXPECT_EQ(tracker.AcknowledgeTeleport(1, t + 3s), Result::Duplicate);
		EXPECT_EQ(tracker.AcknowledgeTeleport(99, t + 3s), Result::Unexpected);
	}

	TEST(PlayerSynchronizationTrackerTests, SequentialTeleportsUseTheNewestPendingId) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		tracker.ExpectTeleport(1, t);
		ASSERT_EQ(tracker.AcknowledgeTeleport(1, t + 1s), Result::Accepted);
		tracker.ExpectTeleport(8, t + 3s);
		EXPECT_EQ(tracker.AcknowledgeTeleport(1, t + 4s), Result::Unexpected);
		EXPECT_EQ(tracker.AcknowledgeTeleport(8, t + 5s), Result::Accepted);
	}

	TEST(PlayerSynchronizationTrackerTests, ChunkFeedbackIsAdvisoryAndAcceptsZeroRate) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(10.0F, t), Result::Unexpected);
		tracker.ExpectChunkBatch(t);
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(0.0F, t + 1s), Result::Accepted);
		ASSERT_TRUE(tracker.PreferredChunksPerTick());
		EXPECT_FLOAT_EQ(*tracker.PreferredChunksPerTick(), 0.0F);
		EXPECT_FALSE(tracker.AwaitingChunkBatchFeedback());
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(12.0F, t + 2s), Result::Duplicate);
		EXPECT_FLOAT_EQ(*tracker.PreferredChunksPerTick(), 0.0F);
	}

	TEST(PlayerSynchronizationTrackerTests, InvalidFeedbackCannotCompletePendingBatch) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		tracker.ExpectChunkBatch(t);
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(-1.0F, t + 1s), Result::Invalid);
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(std::numeric_limits<float>::infinity(), t + 1s), Result::Invalid);
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(std::numeric_limits<float>::quiet_NaN(), t + 1s), Result::Invalid);
		EXPECT_TRUE(tracker.AwaitingChunkBatchFeedback());
		EXPECT_FALSE(tracker.PreferredChunksPerTick());
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(8.0F, t + 2s), Result::Accepted);
	}

	TEST(PlayerSynchronizationTrackerTests, LoadedFlagIsIndependentAndIdempotent) {
		PlayerSynchronizationTracker tracker;
		EXPECT_FALSE(tracker.ClientReportedLoaded());
		EXPECT_TRUE(tracker.MarkPlayerLoaded());
		EXPECT_FALSE(tracker.MarkPlayerLoaded());
		EXPECT_TRUE(tracker.ClientReportedLoaded());
		EXPECT_FALSE(tracker.PendingTeleportId());
		EXPECT_FALSE(tracker.AwaitingChunkBatchFeedback());
	}

	TEST(PlayerSynchronizationTrackerTests, DeadlinesApplyWithoutSleeping) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		tracker.ExpectTeleport(1, t);
		tracker.ExpectChunkBatch(t);
		EXPECT_FALSE(tracker.Expired(t + 59s + 999ms));
		EXPECT_EQ(tracker.Expired(t + 60s), Expiration::Teleport);
		EXPECT_EQ(tracker.AcknowledgeTeleport(1, t + 60s), Result::Expired);
		EXPECT_EQ(tracker.ReceiveChunkBatchFeedback(8.0F, t + 60s), Result::Expired);
		EXPECT_EQ(tracker.PendingTeleportId(), 1);
		EXPECT_TRUE(tracker.AwaitingChunkBatchFeedback());
	}

	TEST(PlayerSynchronizationTrackerTests, ResolvedTeleportAllowsIndependentChunkTimeout) {
		const auto t = PlayerSynchronizationTracker::Clock::now();
		PlayerSynchronizationTracker tracker;
		tracker.ExpectTeleport(1, t);
		tracker.ExpectChunkBatch(t + 1s);
		EXPECT_EQ(tracker.AcknowledgeTeleport(1, t + 2s), Result::Accepted);
		EXPECT_EQ(tracker.Expired(t + 60s), std::nullopt);
		EXPECT_EQ(tracker.Expired(t + 61s), Expiration::ChunkBatch);
	}
}
