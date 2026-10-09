#include "NetworkLoopbackClient.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

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
		int ToPollTimeout(std::chrono::milliseconds timeout) noexcept {
			if (timeout.count() <= 0) return 0;

			const auto maximum =
				static_cast<long long>(
					std::numeric_limits<int>::max());

			return static_cast<int>(
				std::min<long long>(
					timeout.count(),
					maximum));
		}
	}

	struct LoopbackClient::Impl final {
		~Impl() noexcept {
			Close();
		}

		[[nodiscard]] bool IsConnected() const noexcept {
			return Socket >= 0;
		}

		void Close() noexcept {
			if (Socket < 0)
				return;

			[[maybe_unused]]
			const int shutdown_result =
				::shutdown(Socket, SHUT_RDWR);

			::close(Socket);
			Socket = -1;
		}

		[[nodiscard]]
		WaitResult WaitForSocket(
			bool readable,
			bool writable,
			std::chrono::milliseconds timeout) {

			if (Socket < 0) {
				LastError = ENOTCONN;
				return WaitResult::Failed;
			}

			pollfd descriptor{};
			descriptor.fd = Socket;

			if (readable)
				descriptor.events |= POLLIN;

			if (writable)
				descriptor.events |= POLLOUT;

			while (true) {
				const int result =
					::poll(
						&descriptor,
						1,
						ToPollTimeout(timeout));

				if (result == 0) {
					LastError = ETIMEDOUT;
					return WaitResult::TimedOut;
				}

				if (result < 0) {
					if (errno == EINTR)
						continue;

					LastError = errno;
					return WaitResult::Failed;
				}

				if ((descriptor.revents & POLLNVAL) != 0) {
					LastError = EBADF;
					return WaitResult::Failed;
				}

				if ((descriptor.revents & POLLERR) != 0) {
					int socket_error{ 0 };
					socklen_t length{
						static_cast<socklen_t>(
							sizeof(socket_error))
					};

					if (::getsockopt(
						Socket,
						SOL_SOCKET,
						SO_ERROR,
						&socket_error,
						&length) != 0) {

						LastError = errno;
					}
					else {
						LastError =
							socket_error != 0
							? socket_error
							: ECONNABORTED;
					}

					return WaitResult::Failed;
				}

				if ((descriptor.revents & POLLHUP) != 0
					&& !readable) {

					LastError = ECONNRESET;
					return WaitResult::Failed;
				}

				return WaitResult::Ready;
			}
		}

		int Socket{ -1 };
		int LastError{ 0 };
	};

	LoopbackClient::LoopbackClient()
		: m_Impl(std::make_unique<Impl>()) {}

	LoopbackClient::~LoopbackClient() noexcept = default;

	bool LoopbackClient::IsReady() const noexcept {
		return m_Impl != nullptr;
	}

	bool LoopbackClient::IsConnected() const noexcept {
		return m_Impl != nullptr
			&& m_Impl->IsConnected();
	}

	int LoopbackClient::GetLastError() const noexcept {
		return m_Impl != nullptr
			? m_Impl->LastError
			: ENOMEM;
	}

	bool LoopbackClient::Connect(
		std::string_view address,
		std::uint16_t port,
		std::chrono::milliseconds timeout) {

		if (!IsReady())
			return false;

		Close();

		m_Impl->Socket =
			::socket(
				AF_INET,
				SOCK_STREAM | SOCK_CLOEXEC,
				IPPROTO_TCP);

		if (m_Impl->Socket < 0) {
			m_Impl->LastError = errno;
			return false;
		}

		const int flags =
			::fcntl(
				m_Impl->Socket,
				F_GETFL,
				0);

		if (flags < 0
			|| ::fcntl(
				m_Impl->Socket,
				F_SETFL,
				flags | O_NONBLOCK) != 0) {

			m_Impl->LastError = errno;
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

			m_Impl->LastError = EINVAL;
			Close();
			return false;
		}

		const int connect_result =
			::connect(
				m_Impl->Socket,
				reinterpret_cast<const sockaddr*>(&endpoint),
				sizeof(endpoint));

		if (connect_result != 0) {
			const int error = errno;

			if (error != EINPROGRESS
				&& error != EALREADY
				&& error != EWOULDBLOCK) {

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
			socklen_t socket_error_size{
				static_cast<socklen_t>(
					sizeof(socket_error))
			};

			if (::getsockopt(
				m_Impl->Socket,
				SOL_SOCKET,
				SO_ERROR,
				&socket_error,
				&socket_error_size) != 0) {

				m_Impl->LastError = errno;
				Close();
				return false;
			}

			if (socket_error != 0) {
				m_Impl->LastError = socket_error;
				Close();
				return false;
			}
		}

		const int no_delay{ 1 };

		if (::setsockopt(
			m_Impl->Socket,
			IPPROTO_TCP,
			TCP_NODELAY,
			&no_delay,
			sizeof(no_delay)) != 0) {

			m_Impl->LastError = errno;
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
			m_Impl->LastError = ENOTCONN;
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

			const std::size_t request_size =
				std::min(
					remaining,
					static_cast<std::size_t>(
						std::numeric_limits<ssize_t>::max()));

			const ssize_t sent =
				::send(
					m_Impl->Socket,
					data.data() + offset,
					request_size,
					MSG_NOSIGNAL);

			if (sent > 0) {
				offset +=
					static_cast<std::size_t>(sent);

				continue;
			}

			if (sent == 0) {
				m_Impl->LastError = ECONNRESET;
				return false;
			}

			const int error = errno;

			if (error == EINTR
				|| error == EAGAIN
				|| error == EWOULDBLOCK) {

				continue;
			}

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
			m_Impl->LastError = ENOTCONN;
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

			const std::size_t request_size =
				std::min(
					maximum_bytes,
					static_cast<std::size_t>(
						std::numeric_limits<ssize_t>::max()));

			std::vector<std::byte> result(request_size);

			const ssize_t received =
				::recv(
					m_Impl->Socket,
					result.data(),
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

			const int error = errno;

			if (error == EINTR
				|| error == EAGAIN
				|| error == EWOULDBLOCK) {

				continue;
			}

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
				m_Impl->LastError = ECONNRESET;
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
			m_Impl->LastError = EMSGSIZE;
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

			if (m_Impl->LastError == ECONNRESET
				|| m_Impl->LastError == ECONNABORTED
				|| m_Impl->LastError == ENOTCONN
				|| m_Impl->LastError == ESHUTDOWN
				|| m_Impl->LastError == EPIPE) {

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
