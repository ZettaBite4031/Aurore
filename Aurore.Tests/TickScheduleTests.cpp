#include <Aurore/Core/Tick.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace Aurore::Tests {
#pragma region Tick Schedule Tests
	using Aurore::Core::TickSchedule;

	TEST(TickScheduleTests, StartsUninitialized) {
		TickSchedule schedule;

		EXPECT_FALSE(schedule.IsInitialized());

		EXPECT_EQ(schedule.GetTickCount(), 0);
	}

	TEST(TickScheduleTests, InitializesOnFirstTick) {
		TickSchedule schedule;

		const auto start = TickSchedule::TimePoint{};
		const auto context = schedule.BeginTick(start);

		EXPECT_TRUE(schedule.IsInitialized());
		EXPECT_EQ(context.Number, 1);

		EXPECT_EQ(context.ScheduledStart, start);
		EXPECT_EQ(context.ActualStart, start);

		EXPECT_EQ(context.NextTickTime, start + TickSchedule::TickInterval);
		EXPECT_EQ(context.Lateness, TickSchedule::Duration::zero());

		EXPECT_FALSE(context.TimingReset);
	}

	TEST(TickScheduleTests, AdvancesUsingFixedFiftyMsIntervals) {
		TickSchedule schedule;

		const auto start = TickSchedule::TimePoint{};

		const auto first = schedule.BeginTick(start);
		const auto second = schedule.BeginTick(first.NextTickTime);

		EXPECT_EQ(second.Number, 2);
		EXPECT_EQ(second.ScheduledStart, start + TickSchedule::TickInterval);
		EXPECT_EQ(second.NextTickTime, start + TickSchedule::TickInterval * 2);
		EXPECT_FALSE(second.TimingReset);
	}

	TEST(TickScheduleTests, RecordsLatenessWithoutChangingTheSchedule) {
		TickSchedule schedule;

		const auto start = TickSchedule::TimePoint{};

		const auto first = schedule.BeginTick(start);
		const auto currentTime = first.NextTickTime + std::chrono::milliseconds(125);
		const auto second = schedule.BeginTick(currentTime);

		EXPECT_EQ(second.Lateness, std::chrono::milliseconds(125));
		EXPECT_EQ(second.ScheduledStart, first.NextTickTime);
		EXPECT_EQ(second.NextTickTime, first.NextTickTime + TickSchedule::TickInterval);
		EXPECT_FALSE(second.TimingReset);
	}

	TEST(TickScheduleTests, DoesNotResetAtExactlyMaximumLag) {
		TickSchedule schedule;

		const auto start = TickSchedule::TimePoint{};

		const auto first = schedule.BeginTick(start);
		const auto currentTime = first.NextTickTime + TickSchedule::MaximumLag;
		const auto second = schedule.BeginTick(currentTime);

		EXPECT_EQ(second.Lateness, TickSchedule::MaximumLag);
		EXPECT_FALSE(second.TimingReset);
		EXPECT_EQ(second.ScheduledStart, first.NextTickTime);
	}

	TEST(TickScheduleTests, ResetsWhenMoreThanMaximumLagBehind) {
		TickSchedule schedule;

		const auto start = TickSchedule::TimePoint{};
		const auto first = schedule.BeginTick(start);
		const auto currentTime = first.NextTickTime + TickSchedule::MaximumLag + std::chrono::milliseconds(1);
		const auto second = schedule.BeginTick(currentTime);

		EXPECT_TRUE(second.TimingReset);
		EXPECT_EQ(second.ScheduledStart, currentTime);
		EXPECT_EQ(second.NextTickTime, currentTime + TickSchedule::TickInterval);
		EXPECT_EQ(second.Lateness, TickSchedule::Duration::zero());
		EXPECT_EQ(second.Number, 2);
	}

	TEST(TickScheduleTests, ResetRestartsTickNumberingAndTimeline) {
		TickSchedule schedule;

		const auto originalStart = TickSchedule::TimePoint{};

		(void)schedule.BeginTick(originalStart);
		(void)schedule.BeginTick(originalStart + TickSchedule::TickInterval);

		const auto resetTime = originalStart + std::chrono::seconds(10);
		schedule.Reset(resetTime);

		EXPECT_TRUE(schedule.IsInitialized());
		EXPECT_EQ(schedule.GetTickCount(), 0);
		EXPECT_EQ(schedule.GetNextTickTime(), resetTime);

		const auto context = schedule.BeginTick(resetTime);

		EXPECT_EQ(context.Number, 1);
		EXPECT_EQ(context.ScheduledStart, resetTime);
		EXPECT_EQ(context.NextTickTime, resetTime + TickSchedule::TickInterval);
	}
#pragma endregion
}
