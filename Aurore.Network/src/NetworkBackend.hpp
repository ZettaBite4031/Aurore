#pragma once

#include <Aurore/Network/NetworkTypes.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Network::Detail {
	enum class QueuePushResult : std::uint8_t {
		Queued,
		Closed,
		EntryLimitExceeded,
		ByteLimitExceeded,
	};

	struct QueueLimits final {
		std::size_t MaximumEntries{ std::numeric_limits<std::size_t>::max() };
		std::size_t MaximumBytes{ std::numeric_limits<std::size_t>::max() };
	};

	template<typename T, typename TWeight>
	class ConcurrentQueue final {
	public:
		ConcurrentQueue() = default;

		ConcurrentQueue(const ConcurrentQueue&) = delete;
		ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

		[[nodiscard]] QueuePushResult Push(T value) {
			const std::size_t weight = TWeight{}(value);
			std::scoped_lock lock(m_Mutex);
			if (m_Closed) return QueuePushResult::Closed;
			if (m_Values.size() >= m_Limits.MaximumEntries) return QueuePushResult::EntryLimitExceeded;
			if (weight > m_Limits.MaximumBytes || m_QueuedBytes > m_Limits.MaximumBytes - weight) return QueuePushResult::ByteLimitExceeded;
			m_Values.push_back(std::move(value));
			m_QueuedBytes += weight;
			return QueuePushResult::Queued;
		}

		[[nodiscard]] std::optional<T> TryPop() {
			std::scoped_lock lock(m_Mutex);
			if (m_Values.empty()) return std::nullopt;
			const std::size_t weight = TWeight{}(m_Values.front());
			T value = std::move(m_Values.front());
			m_Values.pop_front();
			assert(weight <= m_QueuedBytes);
			m_QueuedBytes = weight <= m_QueuedBytes ? m_QueuedBytes - weight : 0;
			return value;
		}

		[[nodiscard]] std::vector<T> Drain() {
			std::scoped_lock lock(m_Mutex);
			std::vector<T> result;
			result.reserve(m_Values.size());
			while (!m_Values.empty()) {
				result.push_back(std::move(m_Values.front()));
				m_Values.pop_front();
			}
			m_QueuedBytes = 0;
			return result;
		}

		void Clear() noexcept {
			std::scoped_lock lock(m_Mutex);
			m_Values.clear();
			m_QueuedBytes = 0;
		}

		void Close() noexcept {
			std::scoped_lock lock(m_Mutex);
			m_Closed = true;
		}

		void Reset(QueueLimits limits = {}) noexcept {
			std::scoped_lock lock(m_Mutex);
			m_Values.clear();
			m_Limits = limits;
			m_QueuedBytes = 0;
			m_Closed = false;
		}

		[[nodiscard]] bool Empty() const noexcept {
			std::scoped_lock lock(m_Mutex);
			return m_Values.empty();
		}

		[[nodiscard]] bool IsClosed() const noexcept {
			std::scoped_lock lock(m_Mutex);
			return m_Closed;
		}

		[[nodiscard]] NetworkQueueSnapshot GetSnapshot() const noexcept {
			std::scoped_lock lock(m_Mutex);
			return NetworkQueueSnapshot{ .Entries = m_Values.size(), .Bytes = m_QueuedBytes, .Closed = m_Closed };
		}

	private:
		mutable std::mutex m_Mutex;
		std::deque<T> m_Values;
		QueueLimits m_Limits;
		std::size_t m_QueuedBytes{ 0 };
		bool m_Closed{ false };
	};

	class NetworkResourceLedger final {
	public:
		enum class ReservationKind : std::uint8_t {
			Outbound,
			InboundEvent,
		};

		enum class ReserveError : std::uint8_t {
			InvalidConnection,
			PerConnectionLimitExceeded,
			GlobalLimitExceeded,
		};

		struct Limits final {
			std::size_t MaximumOutboundBytesPerConnection{ 0 };
			std::size_t MaximumInboundEventBytesPerConnection{ 0 };
			std::size_t MaximumTotalOutboundBytes{ 0 };
			std::size_t MaximumTotalInboundEventBytes{ 0 };
		};

		class Reservation final {
		public:
			Reservation() = default;
			~Reservation() noexcept {
				Reset();
			}

			Reservation(const Reservation&) = delete;
			Reservation& operator=(const Reservation&) = delete;

			Reservation(Reservation&& other) noexcept {
				MoveFrom(std::move(other));
			}

			Reservation& operator=(Reservation&& other) noexcept {
				if (this == &other) return *this;
				Reset();
				MoveFrom(std::move(other));
				return *this;
			}

			[[nodiscard]] std::size_t RemainingBytes() const noexcept {
				return m_Bytes;
			}

			[[nodiscard]] explicit operator bool() const noexcept {
				return m_Ledger != nullptr && m_Bytes != 0;
			}

			void Release(std::size_t bytes) noexcept {
				if (m_Ledger == nullptr || bytes == 0) return;
				assert(bytes <= m_Bytes);
				if (bytes > m_Bytes) bytes = m_Bytes;
				m_Ledger->Release(m_Kind, m_Connection, bytes);
				m_Bytes -= bytes;
				if (m_Bytes == 0) {
					m_Ledger = nullptr;
					m_Connection = {};
				}
			}

			void Reset() noexcept {
				if (m_Ledger != nullptr && m_Bytes != 0) m_Ledger->Release(m_Kind, m_Connection, m_Bytes);
				m_Ledger = nullptr;
				m_Connection = {};
				m_Bytes = 0;
			}

		private:
			friend class NetworkResourceLedger;

			Reservation(NetworkResourceLedger& ledger, ReservationKind kind, ConnectionId connection, std::size_t bytes) noexcept
				: m_Ledger(&ledger), m_Kind(kind), m_Connection(connection), m_Bytes(bytes) {}

			void MoveFrom(Reservation&& other) noexcept {
				m_Ledger = other.m_Ledger;
				m_Kind = other.m_Kind;
				m_Connection = other.m_Connection;
				m_Bytes = other.m_Bytes;
				other.m_Ledger = nullptr;
				other.m_Connection = {};
				other.m_Bytes = 0;
			}

			NetworkResourceLedger* m_Ledger{ nullptr };
			ReservationKind m_Kind{ ReservationKind::Outbound };
			ConnectionId m_Connection;
			std::size_t m_Bytes{ 0 };
		};

		using ReserveResult = std::expected<Reservation, ReserveError>;

		NetworkResourceLedger() = default;
		NetworkResourceLedger(const NetworkResourceLedger&) = delete;
		NetworkResourceLedger& operator=(const NetworkResourceLedger&) = delete;

		[[nodiscard]] bool Reset(Limits limits) noexcept {
			std::scoped_lock lock(m_Mutex);
			if (!m_Connections.empty() || m_TotalOutboundBytes != 0 || m_TotalInboundEventBytes != 0) return false;
			m_Limits = limits;
			return true;
		}

		[[nodiscard]] bool RegisterConnection(ConnectionId connection) {
			if (!connection) return false;
			std::scoped_lock lock(m_Mutex);
			auto [iterator, inserted] = m_Connections.try_emplace(connection);
			if (inserted) {
				iterator->second.Active = true;
				return true;
			}
			auto& usage = iterator->second;
			if (usage.Active || usage.OutboundBytes != 0 || usage.InboundEventBytes != 0) return false;
			usage.Active = true;
			return true;
		}

		void DeactivateConnection(ConnectionId connection) noexcept {
			std::scoped_lock lock(m_Mutex);
			const auto iterator = m_Connections.find(connection);
			if (iterator == m_Connections.end()) return;
			iterator->second.Active = false;
			TryEraseUnusedConnection(iterator);
		}

		[[nodiscard]] ReserveResult ReserveOutbound(ConnectionId connection, std::size_t bytes) {
			return Reserve(ReservationKind::Outbound, connection, bytes);
		}

		[[nodiscard]] ReserveResult ReserveInboundEvent(ConnectionId connection, std::size_t bytes) {
			return Reserve(ReservationKind::InboundEvent, connection, bytes);
		}

		[[nodiscard]] NetworkResourceSnapshot GetSnapshot() const noexcept {
			std::scoped_lock lock(m_Mutex);
			std::size_t active_connections{ 0 };
			for (const auto& [connection, usage] : m_Connections) {
				(void)connection;
				if (usage.Active) active_connections++;
			}
			return NetworkResourceSnapshot{
				.TrackedConnections = m_Connections.size(),
				.ActiveConnections = active_connections,
				.TotalOutboundBytes = m_TotalOutboundBytes,
				.TotalInboundEventBytes = m_TotalInboundEventBytes,
			};
		}

	private:
		struct ConnectionUsage final {
			bool Active{ false };
			std::size_t OutboundBytes{ 0 };
			std::size_t InboundEventBytes{ 0 };
		};

		using ConnectionMap = std::unordered_map<ConnectionId, ConnectionUsage>;
		using ConnectionIterator = ConnectionMap::iterator;

		[[nodiscard]] ReserveResult Reserve(ReservationKind kind, ConnectionId connection, std::size_t bytes) {
			if (!connection) return std::unexpected(ReserveError::InvalidConnection);
			if (bytes == 0) return Reservation{};
			std::scoped_lock lock(m_Mutex);
			const auto iterator = m_Connections.find(connection);
			if (iterator == m_Connections.end() || !iterator->second.Active) return std::unexpected(ReserveError::InvalidConnection);

			auto& usage = iterator->second;
			std::size_t* connection_bytes{ nullptr };
			std::size_t* total_bytes{ nullptr };
			std::size_t connection_limit{ 0 };
			std::size_t total_limit{ 0 };

			switch (kind) {
			case ReservationKind::Outbound:
				connection_bytes = &usage.OutboundBytes;
				total_bytes = &m_TotalOutboundBytes;
				connection_limit = m_Limits.MaximumOutboundBytesPerConnection;
				total_limit = m_Limits.MaximumTotalOutboundBytes;
				break;
			case ReservationKind::InboundEvent:
				connection_bytes = &usage.InboundEventBytes;
				total_bytes = &m_TotalInboundEventBytes;
				connection_limit = m_Limits.MaximumInboundEventBytesPerConnection;
				total_limit = m_Limits.MaximumTotalInboundEventBytes;
				break;
			}

			if (bytes > connection_limit || *connection_bytes > connection_limit - bytes) return std::unexpected(ReserveError::PerConnectionLimitExceeded);
			if (bytes > total_limit || *total_bytes > total_limit - bytes) return std::unexpected(ReserveError::GlobalLimitExceeded);

			*connection_bytes += bytes;
			*total_bytes += bytes;
			return Reservation(*this, kind, connection, bytes);
		}

		void Release(ReservationKind kind, ConnectionId connection, std::size_t bytes) noexcept {
			std::scoped_lock lock(m_Mutex);
			const auto iterator = m_Connections.find(connection);
			assert(iterator != m_Connections.end());
			if (iterator == m_Connections.end()) return;

			auto& usage = iterator->second;
			std::size_t* connection_bytes{ nullptr };
			std::size_t* total_bytes{ nullptr };
			if (kind == ReservationKind::Outbound) {
				connection_bytes = &usage.OutboundBytes;
				total_bytes = &m_TotalOutboundBytes;
			}
			else {
				connection_bytes = &usage.InboundEventBytes;
				total_bytes = &m_TotalInboundEventBytes;
			}

			assert(bytes <= *connection_bytes);
			assert(bytes <= *total_bytes);
			if (bytes > *connection_bytes || bytes > *total_bytes) return;
			*connection_bytes -= bytes;
			*total_bytes -= bytes;
			TryEraseUnusedConnection(iterator);
		}

		void TryEraseUnusedConnection(ConnectionIterator iterator) {
			const auto& usage = iterator->second;
			if (usage.Active || usage.OutboundBytes != 0 || usage.InboundEventBytes != 0) return;
			m_Connections.erase(iterator);
		}

		mutable std::mutex m_Mutex;
		Limits m_Limits;
		ConnectionMap m_Connections;
		std::size_t m_TotalOutboundBytes{ 0 };
		std::size_t m_TotalInboundEventBytes{ 0 };
	};

	struct SendCommand final {
		ConnectionId Connection;
		Aurore::Util::ByteBuffer Data;
		NetworkResourceLedger::Reservation OutboundReservation;
	};

	struct CloseAfterFlushCommand final {
		ConnectionId Connection;
		ConnectionCloseReason Reason{ ConnectionCloseReason::ApplicationRequested };
	};

	struct CloseImmediatelyCommand final {
		ConnectionId Connection;
		ConnectionCloseReason Reason{ ConnectionCloseReason::ApplicationRequested };
	};

	/*
		Produced internally when the server thread removes a BytesReceivedEvent from the event queue.
		The backend must not post another receive for the connection until this command is consumed.
	*/
	struct ResumeReceiveCommand final {
		ConnectionId Connection;
	};

	struct StopCommand final {};

	using NetworkCommand = std::variant<SendCommand, CloseAfterFlushCommand, CloseImmediatelyCommand, StopCommand, ResumeReceiveCommand>;

	struct NetworkCommandWeight final {
		[[nodiscard]] std::size_t operator()(const NetworkCommand& command) const noexcept {
			return std::visit([](const auto& value) -> std::size_t {
				using T = std::remove_cvref_t<decltype(value)>;
				if constexpr (std::same_as<T, SendCommand>) return value.Data.Size();
				return 0;
			}, command);
		}
	};

	struct QueuedNetworkEvent final {
		NetworkEvent Event;
		NetworkResourceLedger::Reservation InboundReservation;
	};

	struct NetworkEventWeight final {
		[[nodiscard]] std::size_t operator()(const QueuedNetworkEvent& queued) const noexcept {
			return std::visit([](const auto& event) -> std::size_t {
				using T = std::remove_cvref_t<decltype(event)>;
				if constexpr (std::same_as<T, BytesReceivedEvent>) return event.Data.size();
				else if constexpr (std::same_as<T, ConnectionOpenedEvent>) return event.LocalEndpoint.Address.size() + event.RemoteEndpoint.Address.size();
				else if constexpr (std::same_as<T, ConnectionClosedEvent>) return event.Detail.size();
				else if constexpr (std::same_as<T, NetworkFailureEvent>) return event.Message.size();
				return 0;
			}, queued.Event);
		}
	};

	using NetworkCommandQueue = ConcurrentQueue<NetworkCommand, NetworkCommandWeight>;
	using NetworkEventQueue = ConcurrentQueue<QueuedNetworkEvent, NetworkEventWeight>;

	class NetworkBackend {
	public:
		virtual ~NetworkBackend() noexcept = default;

		NetworkBackend(const NetworkBackend&) = delete;
		NetworkBackend& operator=(const NetworkBackend&) = delete;

		[[nodiscard]] virtual NetworkResult<void> Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) = 0;
		[[nodiscard]] virtual NetworkResult<NetworkEndpoint> Start() = 0;

		virtual void NotifyCommandAvailable() noexcept = 0;
		virtual void Stop() noexcept = 0;
		virtual void Shutdown() noexcept = 0;

	protected:
		NetworkBackend() = default;
	};

	[[nodiscard]] NetworkResult<std::unique_ptr<NetworkBackend>> CreateNetworkBackend(NetworkBackendType type);
}

