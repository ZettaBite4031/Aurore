#pragma once

#if !defined(_WIN32)
#error WindowsTypes.hpp is only available on Windows.
#endif

#include "../NetworkBackend.hpp"
#include "WindowsUtils.hpp"

#include <Aurore/Util/ByteBuffer.hpp>

#include <MSWSock.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace Aurore::Network::Detail::Windows {
	inline constexpr std::size_t DefaultAcceptDepth{ 8 };
	inline constexpr DWORD AcceptReceiveDataLength{ 0 };
	inline constexpr DWORD AcceptAddressLength{ static_cast<DWORD>(sizeof(sockaddr_storage) + 16) };
	inline constexpr std::size_t AcceptAddressBufferSize{ static_cast<std::size_t>(AcceptAddressLength) * 2 };

	static_assert(AcceptAddressBufferSize <= std::numeric_limits<DWORD>::max());

	enum class CompletionKey : ULONG_PTR {
		Socket = 1,
		Command = 2,
		Startup = 3,
		Stop = 4,
	};

	enum class IoOperationKind : std::uint8_t {
		Accept,
		Receive,
		Send,
	};

	enum class ConnectionCloseMode : std::uint8_t {
		Open,
		AfterFlush,
		Immediate,
	};

	struct IoOperation {
		explicit IoOperation(IoOperationKind kind) noexcept : Kind(kind) {}
		virtual ~IoOperation() = default;

		IoOperation(const IoOperation&) = delete;
		IoOperation& operator=(const IoOperation&) = delete;

		OVERLAPPED Overlapped{};
		IoOperationKind Kind;
	};

	struct AcceptOperation final : IoOperation {
		AcceptOperation() : IoOperation(IoOperationKind::Accept) {}

		UniqueSocket AcceptedSocket;
		std::array<std::byte, AcceptAddressBufferSize> AddressBuffer{};
	};

	struct ReceiveOperation final : IoOperation {
		ReceiveOperation(ConnectionId connection, std::size_t buffer_size) : IoOperation(IoOperationKind::Receive), Connection(connection), Buffer(buffer_size) {
			BufferDescriptor.buf = reinterpret_cast<char*>(Buffer.data());
			BufferDescriptor.len = static_cast<ULONG>(Buffer.size());
		}

		ConnectionId Connection;
		std::vector<std::byte> Buffer;
		WSABUF BufferDescriptor{};
		DWORD Flags{ 0 };
	};

	struct OutboundBuffer final {
		Aurore::Util::ByteBuffer Data;
		NetworkResourceLedger::Reservation OutboundReservation;
	};

	struct SendOperation final : IoOperation {
		SendOperation(ConnectionId connection, OutboundBuffer outbound) : IoOperation(IoOperationKind::Send), Connection(connection), Data(std::move(outbound.Data)), OutboundReservation(std::move(outbound.OutboundReservation)) {}

		[[nodiscard]] std::size_t Size() const noexcept {
			return Data.Size();
		}

		[[nodiscard]] std::size_t Remaining() const noexcept {
			const std::size_t size = Size();
			return Offset < size ? size - Offset : 0;
		}

		void RefreshBuffer() noexcept {
			const auto bytes = Data.Bytes();
			const std::size_t remaining = Remaining();
			const std::size_t request_size = std::min(remaining, static_cast<std::size_t>(std::numeric_limits<ULONG>::max()));
			auto* data = const_cast<std::byte*>(bytes.data());
			BufferDescriptor.buf = reinterpret_cast<char*>(data + Offset);
			BufferDescriptor.len = static_cast<ULONG>(request_size);
		}

		ConnectionId Connection;
		Aurore::Util::ByteBuffer Data;
		NetworkResourceLedger::Reservation OutboundReservation;
		std::size_t Offset{ 0 };
		WSABUF BufferDescriptor{};
	};

	struct BackendConnection final {
		BackendConnection(ConnectionId id, UniqueSocket socket, NetworkEndpoint local, NetworkEndpoint remote) : Id(id), Socket(std::move(socket)), LocalEndpoint(std::move(local)), RemoteEndpoint(std::move(remote)) {}

		BackendConnection(const BackendConnection&) = delete;
		BackendConnection& operator=(const BackendConnection&) = delete;

		BackendConnection(BackendConnection&&) noexcept = default;
		BackendConnection& operator=(BackendConnection&&) noexcept = default;

		ConnectionId Id;
		UniqueSocket Socket;
		NetworkEndpoint LocalEndpoint;
		NetworkEndpoint RemoteEndpoint;

		std::deque<OutboundBuffer> OutboundQueue;

		ConnectionCloseMode CloseMode{ ConnectionCloseMode::Open };
		ConnectionCloseReason RequestedCloseReason{ ConnectionCloseReason::ApplicationRequested };

		bool ReceivePending{ false };
		bool ReceivePaused{ false };
		bool SendPending{ false };
		OVERLAPPED* ReceiveOverlapped{ nullptr };
		bool SocketClosed{ false };
		bool CloseEventEmitted{ false };

		std::size_t PendingOperationCount{ 0 };
	};
}

