#pragma once

#ifndef __linux__
#error LinuxTypes.hpp is only available on Linux.
#endif

#include <array>
#include <cstddef>

#include <sys/epoll.h>

namespace Aurore::Network::Detail::Linux {
	inline constexpr std::size_t WorkerEventCapacity{ 64 };
	inline constexpr int WaitIndefinitely{ -1 };

	using WorkerEventBuffer = std::array<epoll_event, WorkerEventCapacity>;
}
