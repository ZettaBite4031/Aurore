#include <Aurore/Core/Tick.hpp>

namespace Aurore::Core {
	TickSchedule::Context TickSchedule::BeginTick(TimePoint current_time) noexcept {
		if (!m_Initialized) {
			m_NextTickTime = current_time;
			m_Initialized = true;
		}

		auto scheduled_start = m_NextTickTime;
		auto lateness = Duration::zero();
		bool timing_reset = false;

		if (current_time > scheduled_start) {
			lateness = current_time - scheduled_start;
		}

		/*
			Vanilla abandons its accumulated schedule when the server
			falls more than two seconds behind. This prevents the server
			from attempting an excessive number of immediate catch-up
			ticks.

			The comparison is intentionally strict: exactly two seconds
			behind does not reset the schedule.
		*/
		if (lateness > MaximumLag) {
			scheduled_start = current_time;
			lateness = Duration::zero();
			timing_reset = true;
		}

		m_NextTickTime = scheduled_start + TickInterval;
		m_TickCount++;

		return Context{
			.Number = m_TickCount,
			.ScheduledStart = scheduled_start,
			.ActualStart = current_time,
			.NextTickTime = m_NextTickTime,
			.Lateness = lateness,
			.TimingReset = timing_reset
		};
	}

	void TickSchedule::Reset(TimePoint current_time) noexcept {
		m_TickCount = 0;
		m_NextTickTime = current_time;
		m_Initialized = true;
	}

	bool TickSchedule::IsInitialized() const noexcept {
		return m_Initialized;
	}

	TickNumber TickSchedule::GetTickCount() const noexcept {
		return m_TickCount;
	}

	TickSchedule::TimePoint TickSchedule::GetNextTickTime() const noexcept {
		return m_NextTickTime;
	}
}
