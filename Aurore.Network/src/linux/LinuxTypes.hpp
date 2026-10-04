#pragma once

#ifndef __linux__
#error LinuxTypes.hpp is only available on Linux.
#endif

#include "LinuxUtils.hpp"

#include <Aurore/Network/NetworkTypes.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <sys/epoll.h>

namespace Aurore::Network::Detail::Linux {
	inline constexpr std::size_t WorkerEventCapacity{ 64 };
	inline constexpr int WaitIndefinitely{ -1 };

	inline constexpr std::uint64_t CommandEventToken{ 0 };
	inline constexpr std::uint64_t ListenerEventToken{ std::numeric_limits<std::uint64_t>::max() };

	inline constexpr std::uint32_t ListenerEvents{ EPOLLIN };

	/*
	 * Client reads deliberately remain disabled during step 2.
	 * Step 3 will add EPOLLIN and EPOLLOUT according to connection
	 * backpressure and outbound queue state.
	 */
	inline constexpr std::uint32_t DormantConnectionEvents{ EPOLLRDHUP };

	using WorkerEventBuffer = std::array<epoll_event, WorkerEventCapacity>;

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
		bool CloseEventEmitted{ false };
	};
}
