#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace Aurore::Core {
	// Pure per-connection state. Core owns clock scheduling and transport policy.
	class KeepAliveTracker final {
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		struct Durations final {
			std::chrono::milliseconds Interval{ 15'000 };
			std::chrono::milliseconds Deadline{ 20'000 };
		};

		explicit KeepAliveTracker(TimePoint initial_time, Durations durations)
			: m_LastSentAt(initial_time), m_Durations(durations) {}

		[[nodiscard]] bool ShouldSend(TimePoint now) const noexcept {
			return !m_PendingId && now >= m_LastSentAt && now - m_LastSentAt >= m_Durations.Interval;
		}

		[[nodiscard]] bool IsExpired(TimePoint now) const noexcept {
			return m_PendingId && now >= m_LastSentAt && now - m_LastSentAt >= m_Durations.Deadline;
		}

		void MarkSent(std::int64_t id, TimePoint now) noexcept {
			m_PendingId = id;
			m_LastSentAt = now;
		}

		[[nodiscard]] bool Acknowledge(std::int64_t id, TimePoint now) noexcept {
			if (!m_PendingId || *m_PendingId != id || now < m_LastSentAt || now - m_LastSentAt >= m_Durations.Deadline) return false;
			m_LastRtt = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastSentAt);
			m_PendingId.reset();
			return true;
		}

		[[nodiscard]] std::optional<std::int64_t> PendingId() const noexcept { return m_PendingId; }
		[[nodiscard]] std::optional<std::chrono::milliseconds> LastRtt() const noexcept { return m_LastRtt; }

	private:
		TimePoint m_LastSentAt;
		Durations m_Durations;

		std::optional<std::int64_t> m_PendingId;
		std::optional<std::chrono::milliseconds> m_LastRtt;
	};
}
