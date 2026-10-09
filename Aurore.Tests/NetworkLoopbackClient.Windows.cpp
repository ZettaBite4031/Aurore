#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "NetworkLoopbackClient.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace Aurore::Tests {
	namespace {
		enum class WaitResult : std::uint8_t {
			Ready,
			TimedOut,
			Failed,
		};

		[[nodiscard]]
		std::chrono::milliseconds RemainingUntil(
			std::chrono::steady_clock::time_point deadline) {

			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline) return std::chrono::milliseconds{ 0 };

			auto remaining =
				std::chrono::duration_cast<std::chrono::milliseconds>(
					deadline - now);

			if (remaining.count() == 0)
				remaining = std::chrono::milliseconds{ 1 };

			return remaining;
		}

		[[nodiscard]]
		timeval MakeTimeval(std::chrono::milliseconds timeout) {
			const auto seconds =
				std::chrono::duration_cast<std::chrono::seconds>(timeout);

			const auto microseconds =
				std::chrono::duration_cast<std::chrono::microseconds>(
					timeout - seconds);

			timeval result{};
			result.tv_sec = static_cast<long>(seconds.count());
			result.tv_usec = static_cast<long>(microseconds.count());
			return result;
		}
	}

	struct LoopbackClient::Impl final {
		Impl() {
			WSADATA data{};

			const int result =
				WSAStartup(MAKEWORD(2, 2), &data);

			if (result != 0) {
				LastError = result;
				return;
			}

			if (LOBYTE(data.wVersion) != 2
				|| HIBYTE(data.wVersion) != 2) {

				LastError = WSAVERNOTSUPPORTED;
				WSACleanup();
				return;
			}

			WinsockInitialized = true;
		}

		~Impl() noexcept {
			Close();

			if (WinsockInitialized)
				WSACleanup();
		}

		[[nodiscard]] bool IsConnected() const noexcept {
			return Socket != INVALID_SOCKET;
		}

		void Close() noexcept {
			if (Socket == INVALID_SOCKET)
				return;

			[[maybe_unused]]
			const int shutdown_result =
				::shutdown(Socket, SD_BOTH);

			::closesocket(Socket);
			Socket = INVALID_SOCKET;
		}

		[[nodiscard]]
		WaitResult WaitForSocket(
			bool readable,
			bool writable,
			std::chrono::milliseconds timeout) {

			if (Socket == INVALID_SOCKET) {
				LastError = WSAENOTCONN;
				return WaitResult::Failed;
			}

			fd_set read_set;
			fd_set write_set;
			fd_set exception_set;

			FD_ZERO(&read_set);
			FD_ZERO(&write_set);
			FD_ZERO(&exception_set);

			if (readable)
				FD_SET(Socket, &read_set);

			if (writable)
				FD_SET(Socket, &write_set);

			FD_SET(Socket, &exception_set);

			timeval timeval_value =
				MakeTimeval(timeout);

			const int result = ::select(
				0,
				readable ? &read_set : nullptr,
				writable ? &write_set : nullptr,
				&exception_set,
				&timeval_value);

			if (result == 0) {
				LastError = WSAETIMEDOUT;
				return WaitResult::TimedOut;
			}

			if (result == SOCKET_ERROR) {
				LastError = WSAGetLastError();
				return WaitResult::Failed;
			}

			if (FD_ISSET(Socket, &exception_set)) {
				int socket_error{ 0 };
				int socket_error_size{
					static_cast<int>(sizeof(socket_error))
				};

				if (::getsockopt(
					Socket,
					SOL_SOCKET,
					SO_ERROR,
					reinterpret_cast<char*>(&socket_error),
					&socket_error_size) == SOCKET_ERROR) {

					LastError = WSAGetLastError();
				}
				else {
					LastError =
						socket_error != 0
						? socket_error
						: WSAECONNABORTED;
				}

				return WaitResult::Failed;
			}

			return WaitResult::Ready;
		}

		bool WinsockInitialized{ false };
		SOCKET Socket{ INVALID_SOCKET };
		int LastError{ 0 };
	};

	LoopbackClient::LoopbackClient()
		: m_Impl(std::make_unique<Impl>()) {}

	LoopbackClient::~LoopbackClient() noexcept = default;

	bool LoopbackClient::IsReady() const noexcept {
		return m_Impl != nullptr
			&& m_Impl->WinsockInitialized;
	}

	bool LoopbackClient::IsConnected() const noexcept {
		return m_Impl != nullptr
			&& m_Impl->IsConnected();
	}

	int LoopbackClient::GetLastError() const noexcept {
		return m_Impl != nullptr
			? m_Impl->LastError
			: WSAENOBUFS;
	}

	bool LoopbackClient::Connect(
		std::string_view address,
		std::uint16_t port,
		std::chrono::milliseconds timeout) {

		if (!IsReady())
			return false;

		Close();

		m_Impl->Socket =
			::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (m_Impl->Socket == INVALID_SOCKET) {
			m_Impl->LastError = WSAGetLastError();
			return false;
		}

		u_long nonblocking{ 1 };

		if (::ioctlsocket(
			m_Impl->Socket,
			FIONBIO,
			&nonblocking) == SOCKET_ERROR) {

			m_Impl->LastError = WSAGetLastError();
			Close();
			return false;
		}

		sockaddr_in endpoint{};
		endpoint.sin_family = AF_INET;
		endpoint.sin_port = htons(port);

		const std::string address_string(address);

		if (::inet_pton(
			AF_INET,
			address_string.c_str(),
			&endpoint.sin_addr) != 1) {

			m_Impl->LastError = WSAEINVAL;
			Close();
			return false;
		}

		const int connect_result =
			::connect(
				m_Impl->Socket,
				reinterpret_cast<const sockaddr*>(&endpoint),
				static_cast<int>(sizeof(endpoint)));

		if (connect_result == SOCKET_ERROR) {
			const int error = WSAGetLastError();

			if (error != WSAEWOULDBLOCK
				&& error != WSAEINPROGRESS
				&& error != WSAEALREADY) {

				m_Impl->LastError = error;
				Close();
				return false;
			}

			if (m_Impl->WaitForSocket(
				false,
				true,
				timeout) != WaitResult::Ready) {

				Close();
				return false;
			}

			int socket_error{ 0 };
			int socket_error_size{
				static_cast<int>(sizeof(socket_error))
			};

			if (::getsockopt(
				m_Impl->Socket,
				SOL_SOCKET,
				SO_ERROR,
				reinterpret_cast<char*>(&socket_error),
				&socket_error_size) == SOCKET_ERROR) {

				m_Impl->LastError = WSAGetLastError();
				Close();
				return false;
			}

			if (socket_error != 0) {
				m_Impl->LastError = socket_error;
				Close();
				return false;
			}
		}

		const BOOL no_delay = TRUE;

		if (::setsockopt(
			m_Impl->Socket,
			IPPROTO_TCP,
			TCP_NODELAY,
			reinterpret_cast<const char*>(&no_delay),
			static_cast<int>(sizeof(no_delay))) == SOCKET_ERROR) {

			m_Impl->LastError = WSAGetLastError();
			Close();
			return false;
		}

		m_Impl->LastError = 0;
		return true;
	}

	bool LoopbackClient::SendAll(
		std::span<const std::byte> data,
		std::chrono::milliseconds timeout) {

		if (!IsConnected()) {
			m_Impl->LastError = WSAENOTCONN;
			return false;
		}

		const auto deadline =
			std::chrono::steady_clock::now() + timeout;

		std::size_t offset{ 0 };

		while (offset < data.size()) {
			if (m_Impl->WaitForSocket(
				false,
				true,
				RemainingUntil(deadline))
				!= WaitResult::Ready) {

				return false;
			}

			const std::size_t remaining =
				data.size() - offset;

			const int request_size =
				static_cast<int>(
					std::min(
						remaining,
						static_cast<std::size_t>(
							std::numeric_limits<int>::max())));

			const int sent =
				::send(
					m_Impl->Socket,
					reinterpret_cast<const char*>(
						data.data() + offset),
					request_size,
					0);

			if (sent > 0) {
				offset +=
					static_cast<std::size_t>(sent);

				continue;
			}

			if (sent == 0) {
				m_Impl->LastError = WSAECONNRESET;
				return false;
			}

			const int error = WSAGetLastError();

			if (error == WSAEWOULDBLOCK)
				continue;

			m_Impl->LastError = error;
			return false;
		}

		m_Impl->LastError = 0;
		return true;
	}

	std::optional<std::vector<std::byte>>
	LoopbackClient::ReceiveSome(
		std::size_t maximum_bytes,
		std::chrono::milliseconds timeout) {

		if (!IsConnected()) {
			m_Impl->LastError = WSAENOTCONN;
			return std::nullopt;
		}

		if (maximum_bytes == 0)
			return std::vector<std::byte>{};

		const auto deadline =
			std::chrono::steady_clock::now() + timeout;

		while (true) {
			if (m_Impl->WaitForSocket(
				true,
				false,
				RemainingUntil(deadline))
				!= WaitResult::Ready) {

				return std::nullopt;
			}

			const int request_size =
				static_cast<int>(
					std::min(
						maximum_bytes,
						static_cast<std::size_t>(
							std::numeric_limits<int>::max())));

			std::vector<std::byte> result(
				static_cast<std::size_t>(request_size));

			const int received =
				::recv(
					m_Impl->Socket,
					reinterpret_cast<char*>(result.data()),
					request_size,
					0);

			if (received > 0) {
				result.resize(
					static_cast<std::size_t>(received));

				m_Impl->LastError = 0;
				return result;
			}

			if (received == 0) {
				result.clear();
				m_Impl->LastError = 0;
				return result;
			}

			const int error = WSAGetLastError();

			if (error == WSAEWOULDBLOCK)
				continue;

			m_Impl->LastError = error;
			return std::nullopt;
		}
	}

	std::optional<std::vector<std::byte>>
	LoopbackClient::ReceiveExact(
		std::size_t size,
		std::chrono::milliseconds timeout) {

		std::vector<std::byte> result;
		result.reserve(size);

		const auto deadline =
			std::chrono::steady_clock::now() + timeout;

		while (result.size() < size) {
			auto received = ReceiveSome(
				size - result.size(),
				RemainingUntil(deadline));

			if (!received.has_value())
				return std::nullopt;

			if (received->empty()) {
				m_Impl->LastError = WSAECONNRESET;
				return std::nullopt;
			}

			result.insert(
				result.end(),
				received->begin(),
				received->end());
		}

		return result;
	}

	bool LoopbackClient::WaitForEof(
		std::chrono::milliseconds timeout) {

		auto received = ReceiveSome(
			64 * 1024,
			timeout);

		if (!received.has_value())
			return false;

		if (!received->empty()) {
			m_Impl->LastError = WSAEMSGSIZE;
			return false;
		}

		return true;
	}

	bool LoopbackClient::WaitForDisconnect(
		std::chrono::milliseconds timeout) {

		const auto deadline =
			std::chrono::steady_clock::now() + timeout;

		while (true) {
			auto received = ReceiveSome(
				64 * 1024,
				RemainingUntil(deadline));

			if (received.has_value()) {
				if (received->empty())
					return true;

				continue;
			}

			if (m_Impl->LastError == WSAECONNRESET
				|| m_Impl->LastError == WSAECONNABORTED
				|| m_Impl->LastError == WSAENOTCONN
				|| m_Impl->LastError == WSAESHUTDOWN) {

				return true;
			}

			return false;
		}
	}

	void LoopbackClient::Close() noexcept {
		if (m_Impl != nullptr)
			m_Impl->Close();
	}
}
