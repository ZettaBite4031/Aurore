#pragma once

#ifndef __linux__
#error LinuxUtils.hpp is only available on Linux.
#endif

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <utility>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Aurore::Network::Detail::Linux {
    class UniqueFd final {
    public:
        UniqueFd() = default;

        explicit UniqueFd(int fd) noexcept : m_Fd(fd) {}

        ~UniqueFd() noexcept { Reset(); }

        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;

        UniqueFd(UniqueFd&& other) noexcept : m_Fd(other.Release()) {}
        UniqueFd& operator=(UniqueFd&& other) noexcept { 
            if (this == &other) return *this;

            Reset(other.Release());
            return *this;
        }

        [[nodiscard]] int Get() const noexcept { 
            return m_Fd;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_Fd >= 0;
        }

        [[nodiscard]] int Release() noexcept {
            const int fd = m_Fd;
            m_Fd = -1;
            return fd;
        }

        void Reset(int fd = -1) noexcept {
            if (m_Fd >= 0) ::close(m_Fd);
            m_Fd = fd;
        }

    private:
        int m_Fd{ -1 };
    };

    [[nodiscard]] inline std::string FormatSystemError(int error) {
        const std::error_code code(error, std::generic_category());
        return code.message() + " (" + std::to_string(error) + ")";
    }

    [[nodiscard]] inline UniqueFd CreateEpollInstance() noexcept {
        return UniqueFd(::epoll_create1(EPOLL_CLOEXEC));
    }

    [[nodiscard]] inline UniqueFd CreateEventCounter() noexcept {
        return UniqueFd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    }

    [[nodiscard]] inline bool AddEpollInterest(int epoll_fd, int fd, std::uint32_t events) noexcept {
        if (epoll_fd < 0 || fd < 0) return false;

        epoll_event event{};
        event.events = events;
        event.data.fd = fd;

        return ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
    }

    [[nodiscard]] inline bool ModifyEpollInterest(int epoll_fd, int fd, std::uint32_t events) noexcept {
        if (epoll_fd < 0 || fd < 0) return false;

        epoll_event event{};
        event.events = events;
        event.data.fd = fd;

        return ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
    }

    [[nodiscard]] inline bool RemoveEpollInterest(int epoll_fd, int fd) noexcept {
        if (epoll_fd < 0 || fd < 0) return false;
        return ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }

    [[nodiscard]] inline bool WakeEventCounter(int fd) noexcept {
        if (fd < 0) return false;

        constexpr std::uint64_t WakeValue{ 1 };

        while (true) {
            const ssize_t result = ::write(fd, &WakeValue, sizeof(WakeValue));

            if (result == static_cast<ssize_t>(sizeof(WakeValue))) {
                return true;
            }

            if (result < 0 && errno == EINTR) continue;

            /*
             A saturated eventfd is already readable, so the
             required wakeup is already pending.
             */
            if (result < 0 && errno == EAGAIN) return true;
            return false;
        }
    }

    [[nodiscard]] inline bool DrainEventCounter(int fd) noexcept {
        if (fd < 0) return false;

        std::uint64_t value{ 0 };

        while (true) {
            const ssize_t result = ::read(fd, &value, sizeof(value));
            if (result == static_cast<ssize_t>(sizeof(value))) return true;

            if (result < 0 && errno == EINTR) continue;

            /*
             * There is nothing left to drain. This is harmless
             * when multiple wake requests have collapsed into
             * a single eventfd counter update.
             */
            if (result < 0 && errno == EAGAIN) return true;
            return false;
        }
    }
}
