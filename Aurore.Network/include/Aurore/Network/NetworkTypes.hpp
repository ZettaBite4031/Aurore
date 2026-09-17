#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace Aurore::Network {
	struct ConnectionId final {
		std::uint64_t Value{ 0 };

		[[nodiscard]] operator bool() const noexcept {
			return Value != 0;
		}

		auto operator<=>(const ConnectionId&) const = default;
	};

	struct NetworkEndpoint final {
		std::string Address;
		std::uint16_t Port{ 0 };

		auto operator<=>(const NetworkEndpoint&) const = default;
	};

	enum class ConnectionCloseReason : std::uint8_t {
		RemoteClosed,
		ApplicationRequested,
		ServerStopping,
		TransportError,
		InboundLimitExceeded,
		OutboundLimitExceeded,
		IdleTimeout,
		BackendFailure,
	};

	enum class NetworkError : std::uint8_t {
		InvalidConfiguration,
		InvalidConnectionId,
		EmptyPayload,
		NotInitialized,
		AlreadyInitialized,
		NotRunning,
		AlreadyRunning,
		BackendUnavailable,
		CommandQueueClosed,
		CommandQueueLimitExceeded,
		EventQueueLimitExceeded,
		InboundLimitExceeded,
		OutboundLimitExceeded,
		BackendFailure,
	};

	enum class NetworkBackendType : std::uint8_t {
		Automatic,
		Iocp,
		Epoll,
		Kqueue,
	};

	template<typename T>
	using NetworkResult = std::expected<T, NetworkError>;

	struct NetworkConfiguration final {
		NetworkBackendType Backend{ NetworkBackendType::Automatic };

		std::string BindAddress{ "0.0.0.0" };
		std::uint16_t Port{ 25565 };

		std::size_t MaximumConnections{ 1024 };

		std::size_t ReceiveBufferSize{ 64 * 1024 };
		std::size_t MaximumInboundBytesPerConnection{ 2 * 1024 * 1024 };
		std::size_t MaximumOutboundBytesPerConnection{ 2 * 1024 * 1024 };

		std::size_t MaximumCommandQueueEntries{ 16 * 1024 };
		std::size_t MaximumCommandQueueBytes{ 64 * 1024 * 1024 };
		std::size_t MaximumEventQueueEntries{ 16 * 1024 };
		std::size_t MaximumEventQueueBytes{ 64 * 1024 * 1024 };

		std::size_t MaximumTotalOutboundBytes{ 256 * 1024 * 1024 };
		std::size_t MaximumTotalInboundEventBytes{ 256 * 1024 * 1024 };
	};

	struct NetworkQueueSnapshot final {
		std::size_t Entries{ 0 };
		std::size_t Bytes{ 0 };
		bool Closed{ false };
	};

	struct NetworkResourceSnapshot final {
		std::size_t TrackedConnections{ 0 };
		std::size_t ActiveConnections{ 0 };
		std::size_t TotalOutboundBytes{ 0 };
		std::size_t TotalInboundEventBytes{ 0 };
	};

	struct ConnectionOpenedEvent final {
		ConnectionId Connection;
		NetworkEndpoint LocalEndpoint;
		NetworkEndpoint RemoteEndpoint;
	};

	struct BytesReceivedEvent final {
		ConnectionId Connection;
		std::vector<std::byte> Data;
	};

	struct ConnectionClosedEvent final {
		ConnectionId Connection;
		ConnectionCloseReason Reason{ ConnectionCloseReason::RemoteClosed };
		std::string Detail;
	};

	struct NetworkFailureEvent final {
		NetworkError Error{ NetworkError::BackendFailure };
		std::string Message;
		bool Fatal{ true };
	};

	using NetworkEvent = std::variant<ConnectionOpenedEvent, BytesReceivedEvent, ConnectionClosedEvent, NetworkFailureEvent>;
}

template<>
struct std::hash<Aurore::Network::ConnectionId> {
	std::size_t operator()(Aurore::Network::ConnectionId connection) const noexcept {
		return std::hash<std::uint64_t>{}(connection.Value);
	}
};

