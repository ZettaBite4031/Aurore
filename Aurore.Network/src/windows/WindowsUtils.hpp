#pragma once

#if !defined(_WIN32)
#error WindowsUtils.hpp is only available on Windows.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <Aurore/Network/NetworkTypes.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Aurore::Network::Detail::Windows {
	class WinsockSession final {
	public:
		WinsockSession() = default;
		~WinsockSession() noexcept {
			Reset();
		}

		WinsockSession(const WinsockSession&) = delete;
		WinsockSession& operator=(const WinsockSession&) = delete;

		WinsockSession(WinsockSession&&) = delete;
		WinsockSession& operator=(WinsockSession&&) = delete;

		[[nodiscard]] NetworkResult<void> Initialize() noexcept {
			if (m_Initialized) return {};

			WSADATA data{};
			const int result = WSAStartup(MAKEWORD(2, 2), &data);

			if (result != 0) {
				m_LastError = result;
				return std::unexpected(NetworkError::BackendFailure);
			}

			if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
				WSACleanup();
				m_LastError = WSAVERNOTSUPPORTED;
				return std::unexpected(NetworkError::BackendFailure);
			}

			m_LastError = 0;
			m_Initialized = true;
			return {};
		}

		void Reset() noexcept {
			if (!m_Initialized) return;
			WSACleanup();
			m_Initialized = false;
		}

		[[nodiscard]] bool IsInitialized() const noexcept {
			return m_Initialized;
		}

		[[nodiscard]] int GetLastError() const noexcept {
			return m_LastError;
		}

	private:
		bool m_Initialized{ false };
		int m_LastError{ 0 };
	};

	class UniqueSocket final {
	public:
		UniqueSocket() = default;
		explicit UniqueSocket(SOCKET socket) noexcept
			: m_Socket(socket) {}

		~UniqueSocket() noexcept {
			Reset();
		}

		UniqueSocket(const UniqueSocket&) = delete;
		UniqueSocket& operator=(const UniqueSocket&) = delete;

		UniqueSocket(UniqueSocket&& other) noexcept
			: m_Socket(other.Release()) {}

		UniqueSocket& operator=(UniqueSocket&& other) noexcept {
			if (this == &other) return *this;
			Reset(other.Release());
			return *this;
		}

		[[nodiscard]] SOCKET Get() const noexcept {
			return m_Socket;
		}

		[[nodiscard]] explicit operator bool() const noexcept {
			return m_Socket != INVALID_SOCKET;
		}

		[[nodiscard]] SOCKET Release() noexcept {
			const SOCKET socket = m_Socket;
			m_Socket = INVALID_SOCKET;
			return socket;
		}

		void Reset(SOCKET socket = INVALID_SOCKET) noexcept {
			if (m_Socket != INVALID_SOCKET)
				closesocket(m_Socket);
			m_Socket = socket;
		}

	private:
		SOCKET m_Socket{ INVALID_SOCKET };
	};

	class UniqueHandle final {
	public:
		UniqueHandle() = default;

		explicit UniqueHandle(HANDLE handle) noexcept
			: m_Handle(handle) {}

		~UniqueHandle() noexcept {
			Reset();
		}

		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;

		UniqueHandle(UniqueHandle&& other) noexcept
			: m_Handle(other.Release()) {}

		UniqueHandle& operator=(UniqueHandle&& other) noexcept {
			if (this == &other) return *this;
			Reset(other.Release());
			return *this;
		}

		[[nodiscard]]
		HANDLE Get() const noexcept {
			return m_Handle;
		}

		[[nodiscard]]
		explicit operator bool() const noexcept {
			return m_Handle != nullptr && m_Handle != INVALID_HANDLE_VALUE;
		}

		[[nodiscard]]
		HANDLE Release() noexcept {
			const HANDLE handle = m_Handle;
			m_Handle = nullptr;
			return handle;
		}

		void Reset(HANDLE handle = nullptr) noexcept {
			if (m_Handle != nullptr && m_Handle != INVALID_HANDLE_VALUE) {
				CloseHandle(m_Handle);
			}

			m_Handle = handle;
		}

	private:
		HANDLE m_Handle{ nullptr };
	};

	struct AddressInfoDeleter final {
		void operator()(ADDRINFOA* address_info) const noexcept {
			if (address_info != nullptr)
				freeaddrinfo(address_info);
		}
	};

	using UniqueAddressInfo = std::unique_ptr<ADDRINFOA, AddressInfoDeleter>;

	[[nodiscard]] inline std::string FormatWindowsError(DWORD error) {
		if (error == ERROR_SUCCESS) return "No error";

		char* buffer{ nullptr };
		const DWORD flags{ FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS };
		const DWORD length{ FormatMessageA(flags, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<char*>(&buffer), 0, nullptr) };

		if (length == 0 || buffer == nullptr) {
			return "Windows error " + std::to_string(error);
		}

		std::string message(buffer, length);
		LocalFree(buffer);

		while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
			message.pop_back();

		return message + "(" + std::to_string(error) + ")";
	}

	[[nodiscard]] inline std::string FormatSocketError(int error) {
		return FormatWindowsError(static_cast<DWORD>(error));
	}

	[[nodiscard]] inline std::optional<NetworkEndpoint> MakeNetworkEndpoint(const sockaddr* address, int addr_len) {
		if (address == nullptr || addr_len <= 0) return std::nullopt;

		char host[NI_MAXHOST]{};
		char service[NI_MAXSERV]{};

		const int result = getnameinfo(address, addr_len, host, static_cast<DWORD>(sizeof(host)), service, static_cast<DWORD>(sizeof(service)), NI_NUMERICHOST | NI_NUMERICSERV);
		if (result != 0) return std::nullopt;

		char* end = nullptr;
		const unsigned long port = std::strtoul(service, &end, 10);
		if (end == service || *end != '\0' || port > 65535UL) return std::nullopt;

		return NetworkEndpoint{ .Address = host, .Port = static_cast<std::uint16_t>(port) };
	}

	[[nodiscard]] inline bool SetExclusiveAddressUse(SOCKET socket) noexcept {
		const BOOL enabled = TRUE;
		return setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), static_cast<int>(sizeof(enabled))) != SOCKET_ERROR;
	}

	[[nodiscard]] inline bool SetTcpNoDelay(SOCKET socket) noexcept {
		const BOOL enabled = TRUE;
		return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), static_cast<int>(sizeof(enabled))) != SOCKET_ERROR;
	}

	inline void SetAbortiveClose(SOCKET socket) noexcept {
		if (socket == INVALID_SOCKET) return;

		linger option{};
		option.l_onoff = 1;
		option.l_linger = 0;

		[[maybe_unused]]
		const int result = setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), static_cast<int>(sizeof(option)));
	}
}
