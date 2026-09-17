#pragma once

#include <chrono>
#include <cstdint>

namespace Aurore::Core {
	using TickNumber = std::uint64_t;

	class TickSchedule final {
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;
		using Duration = Clock::duration;

		static constexpr auto TickInterval{ std::chrono::milliseconds(50) };
		static constexpr auto MaximumLag{ std::chrono::seconds(2) };

		struct Context {
			TickNumber Number{ 0 };

			TimePoint ScheduledStart{};
			TimePoint ActualStart{};
			TimePoint NextTickTime{};

			Duration Lateness{};

			bool TimingReset{ false };
		};

		TickSchedule() = default;

		[[nodiscard]] Context BeginTick(TimePoint current_time) noexcept;

		void Reset(TimePoint current_time) noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;

		[[nodiscard]] TickNumber GetTickCount() const noexcept;
		[[nodiscard]] TimePoint GetNextTickTime() const noexcept;

	private:
		TickNumber m_TickCount{ 0 };
		TimePoint m_NextTickTime{};

		bool m_Initialized{ false };
	};
}
