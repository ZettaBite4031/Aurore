#pragma once

#ifndef __linux__
#error LinuxTypes.hpp is only available on Linux.
#endif

#include "../NetworkBackend.hpp"
#include "LinuxUtils.hpp"

#include <Aurore/Util/ByteBuffer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>

#include <sys/epoll.h>

namespace Aurore::Network::Detail::Linux {
	inline constexpr std::size_t WorkerEventCapacity{ 64 };
	inline constexpr int WaitIndefinitely{ -1 };

	inline constexpr std::uint64_t CommandEventToken{ 0 };
	inline constexpr std::uint64_t ListenerEventToken{ std::numeric_limits<std::uint64_t>::max() };

	inline constexpr std::uint32_t ListenerEvents{ EPOLLIN };

	using WorkerEventBuffer = std::array<epoll_event, WorkerEventCapacity>;

	enum class ConnectionCloseMode : std::uint8_t {
		Open,
		AfterFlush,
		Immediate,
	};

	struct OutboundBuffer final {
		Aurore::Util::ByteBuffer Data;
		NetworkResourceLedger::Reservation OutboundReservation;
		std::size_t Offset{ 0 };

		[[nodiscard]] std::size_t Remaining() const noexcept {
			const std::size_t size = Data.Size();
			return Offset < size ? size - Offset : 0;
		}
	};

	struct BackendConnection final {
		BackendConnection(ConnectionId id, UniqueFd socket, NetworkEndpoint local, NetworkEndpoint remote)
			: Id(id), Socket(std::move(socket)), LocalEndpoint(std::move(local)), RemoteEndpoint(std::move(remote)) {}

		BackendConnection(const BackendConnection&) = delete;
		BackendConnection& operator=(const BackendConnection&) = delete;
		BackendConnection(BackendConnection&&) noexcept = default;
		BackendConnection& operator=(BackendConnection&&) noexcept = default;

		ConnectionId Id;
		UniqueFd Socket;
		NetworkEndpoint LocalEndpoint;
		NetworkEndpoint RemoteEndpoint;

		std::deque<OutboundBuffer> OutboundQueue;

		ConnectionCloseMode CloseMode{ ConnectionCloseMode::Open };
		ConnectionCloseReason RequestedCloseReason{ ConnectionCloseReason::ApplicationRequested };

		bool ReceivePaused{ false };
		bool CloseEventEmitted{ false };
	};
}
