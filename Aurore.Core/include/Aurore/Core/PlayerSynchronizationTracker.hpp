#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Aurore::Core {
	// First Light has one outstanding teleport and one outstanding chunk batch.
	// The chunk feedback packet has no batch ID, so only its order can be checked.
	class PlayerSynchronizationTracker final {
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;
		static constexpr auto ResponseDeadline = std::chrono::seconds{ 60 };

		enum class Result : std::uint8_t {
			Accepted,
			Duplicate,
			Unexpected,
			Invalid,
			Expired,
		};

		enum class Expiration : std::uint8_t {
			Teleport,
			ChunkBatch,
		};

		void ExpectTeleport(std::int32_t teleport_id, TimePoint queued_at) noexcept {
			m_PendingTeleportId = teleport_id;
			m_TeleportQueuedAt = queued_at;
		}

		void ExpectChunkBatch(TimePoint queued_at) noexcept {
			m_ChunkBatchPending = true;
			m_ChunkBatchQueuedAt = queued_at;
		}

		[[nodiscard]] Result AcknowledgeTeleport(std::int32_t teleport_id, TimePoint now) noexcept {
			if (!m_PendingTeleportId)
				return m_LastAcknowledgedTeleportId == teleport_id ? Result::Duplicate : Result::Unexpected;
			if (*m_PendingTeleportId != teleport_id) return Result::Unexpected;
			if (HasExpired(m_TeleportQueuedAt, now)) return Result::Expired;
			m_LastAcknowledgedTeleportId = teleport_id;
			m_PendingTeleportId.reset();
			return Result::Accepted;
		}

		[[nodiscard]] Result ReceiveChunkBatchFeedback(float desired_chunks_per_tick, TimePoint now) noexcept {
			// Zero is valid as a flow-control preference. NaN, infinities and
			// negative values must never enter future chunk scheduling logic.
			if (!std::isfinite(desired_chunks_per_tick) || desired_chunks_per_tick < 0.0F)
				return Result::Invalid;
			if (!m_ChunkBatchPending)
				return m_PreferredChunksPerTick ? Result::Duplicate : Result::Unexpected;
			if (HasExpired(m_ChunkBatchQueuedAt, now)) return Result::Expired;
			m_PreferredChunksPerTick = desired_chunks_per_tick;
			m_ChunkBatchPending = false;
			return Result::Accepted;
		}

		// PlayerLoaded is independent of the teleport and chunk-batch replies.
		[[nodiscard]] bool MarkPlayerLoaded() noexcept {
			if (m_ClientReportedLoaded) return false;
			m_ClientReportedLoaded = true;
			return true;
		}

		[[nodiscard]] std::optional<Expiration> Expired(TimePoint now) const noexcept {
			if (m_PendingTeleportId && HasExpired(m_TeleportQueuedAt, now)) return Expiration::Teleport;
			if (m_ChunkBatchPending && HasExpired(m_ChunkBatchQueuedAt, now)) return Expiration::ChunkBatch;
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::int32_t> PendingTeleportId() const noexcept { return m_PendingTeleportId; }
		[[nodiscard]] std::optional<std::int32_t> LastAcknowledgedTeleportId() const noexcept { return m_LastAcknowledgedTeleportId; }
		[[nodiscard]] bool AwaitingChunkBatchFeedback() const noexcept { return m_ChunkBatchPending; }
		[[nodiscard]] bool ClientReportedLoaded() const noexcept { return m_ClientReportedLoaded; }
		[[nodiscard]] std::optional<float> PreferredChunksPerTick() const noexcept { return m_PreferredChunksPerTick; }

	private:
		[[nodiscard]] static bool HasExpired(TimePoint since, TimePoint now) noexcept {
			return now >= since && now - since >= ResponseDeadline;
		}

		std::optional<std::int32_t> m_PendingTeleportId;
		std::optional<std::int32_t> m_LastAcknowledgedTeleportId;
		TimePoint m_TeleportQueuedAt{};
		TimePoint m_ChunkBatchQueuedAt{};
		std::optional<float> m_PreferredChunksPerTick;
		bool m_ChunkBatchPending{ false };
		bool m_ClientReportedLoaded{ false };
	};
}
