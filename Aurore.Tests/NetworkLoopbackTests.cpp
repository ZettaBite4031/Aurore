#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <Aurore/Core/Server.hpp>
#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Network/NetworkManager.hpp>
#include <Aurore/Network/NetworkTypes.hpp>

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <../src/ServerTestAccess.hpp>
#include <NetworkManagerTestAccess.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using namespace std::chrono_literals;

		namespace Configuration =
			Aurore::Protocol::Packets::Configuration;

		namespace Login =
			Aurore::Protocol::Packets::Login;

		using Aurore::Core::ClientLifecycleState;
		using Aurore::Core::InitialSyntheticRegistryGeneration;
		using Aurore::Core::Server;
		using Aurore::Core::Detail::ServerTestAccess;

		using Aurore::Network::BytesReceivedEvent;
		using Aurore::Network::ConnectionCloseReason;
		using Aurore::Network::ConnectionClosedEvent;
		using Aurore::Network::ConnectionId;
		using Aurore::Network::ConnectionOpenedEvent;
		using Aurore::Network::NetworkBackendType;
		using Aurore::Network::NetworkConfiguration;
		using Aurore::Network::NetworkEndpoint;
		using Aurore::Network::NetworkEvent;
		using Aurore::Network::NetworkFailureEvent;
		using Aurore::Network::NetworkManager;
		using Aurore::Network::NetworkResourceSnapshot;
		using Aurore::Network::Detail::NetworkManagerTestAccess;

		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::FrameError;
		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PacketFrameDecoder;
		using Aurore::Protocol::ProtocolState;

		using Aurore::Util::ByteBuffer;
		using Aurore::Util::ByteReader;

		constexpr auto EventTimeout = 3s;
		constexpr auto SocketTimeout = 3s;
		constexpr auto PollInterval = 1ms;

		constexpr std::size_t ReceiveChunkSize =
			64 * 1024;

		constexpr std::size_t MaximumStatusJsonBytes =
			2 * 1024 * 1024;

		[[nodiscard]]
		NetworkConfiguration MakeLoopbackConfig() {
			return NetworkConfiguration{
				.Backend = NetworkBackendType::Iocp,
				.BindAddress = "127.0.0.1",
				.Port = 0,
				.MaximumConnections = 64,
				.ReceiveBufferSize = 4096,
				.MaximumInboundBytesPerConnection =
					2 * 1024 * 1024,
				.MaximumOutboundBytesPerConnection =
					2 * 1024 * 1024,
				.MaximumCommandQueueEntries = 1024,
				.MaximumCommandQueueBytes =
					8 * 1024 * 1024,
				.MaximumEventQueueEntries = 1024,
				.MaximumEventQueueBytes =
					8 * 1024 * 1024,
				.MaximumTotalOutboundBytes =
					32 * 1024 * 1024,
				.MaximumTotalInboundEventBytes =
					32 * 1024 * 1024,
			};
		}

		[[nodiscard]]
		std::chrono::milliseconds RemainingUntil(
			std::chrono::steady_clock::time_point deadline) {

			const auto now =
				std::chrono::steady_clock::now();

			if (now >= deadline)
				return 0ms;

			auto remaining =
				std::chrono::duration_cast<
					std::chrono::milliseconds>(
						deadline - now);

			if (remaining == 0ms)
				remaining = 1ms;

			return remaining;
		}

		[[nodiscard]]
		timeval MakeTimeval(
			std::chrono::milliseconds timeout) {

			const auto seconds =
				std::chrono::duration_cast<
					std::chrono::seconds>(timeout);

			const auto microseconds =
				std::chrono::duration_cast<
					std::chrono::microseconds>(
						timeout - seconds);

			timeval result{};

			result.tv_sec =
				static_cast<long>(seconds.count());

			result.tv_usec =
				static_cast<long>(
					microseconds.count());

			return result;
		}

		[[nodiscard]]
		std::vector<std::byte> MakeBytes(
			std::string_view text) {

			std::vector<std::byte> result;
			result.reserve(text.size());

			for (const char character : text) {
				result.push_back(
					static_cast<std::byte>(
						static_cast<unsigned char>(
							character)));
			}

			return result;
		}

		[[nodiscard]]
		std::vector<std::byte> Combine(
			std::initializer_list<
				std::span<const std::byte>> ranges) {

			std::size_t total_size{ 0 };

			for (const auto range : ranges)
				total_size += range.size();

			std::vector<std::byte> result;
			result.reserve(total_size);

			for (const auto range : ranges) {
				result.insert(
					result.end(),
					range.begin(),
					range.end());
			}

			return result;
		}

		[[nodiscard]]
		std::span<const std::byte> PayloadBytes(
			const PacketFrame& frame) noexcept {

			return std::span<const std::byte>(
				frame.Payload.data(),
				frame.Payload.size());
		}

		[[nodiscard]]
		ByteBuffer MakeStatusHandshakeFrame(
			std::uint16_t port) {

			ByteBuffer payload;

			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(port);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					HandshakeIntention::Status));

			return EncodePacketFrame(
				0x00,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeLoginHandshakeFrame(
			std::uint16_t port) {

			ByteBuffer payload;

			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(port);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					HandshakeIntention::Login));

			return EncodePacketFrame(
				0x00,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeStatusRequestFrame() {
			return EncodePacketFrame(
				0x00,
				std::span<const std::byte>{});
		}

		[[nodiscard]]
		ByteBuffer MakePingRequestFrame(
			std::int64_t value) {

			ByteBuffer payload;
			payload.WriteInt64(value);

			return EncodePacketFrame(
				0x01,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeLoginStartFrame(
			std::string_view username) {

			ByteBuffer payload;

			payload.WriteString(username);
			payload.WriteUuid({});

			return EncodePacketFrame(
				Login::Serverbound::StartPacketId,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeLoginAcknowledgedFrame() {
			return EncodePacketFrame(
				Login::Serverbound::
					AcknowledgedPacketId,
				std::span<const std::byte>{});
		}

		[[nodiscard]]
		ByteBuffer MakeClientInformationFrame() {
			ByteBuffer payload;

			payload.WriteString("en_us");
			payload.WriteUnsignedByte(12);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::ChatMode::Enabled));

			payload.WriteBool(true);
			payload.WriteUnsignedByte(0x7F);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::MainHand::Right));

			payload.WriteBool(false);
			payload.WriteBool(true);

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::
						ParticleStatus::All));

			return EncodePacketFrame(
				Configuration::Serverbound::
					ClientInformationPacketId,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeEmptyKnownPackSelectionFrame() {
			ByteBuffer payload;
			payload.WriteVarInt(0);

			return EncodePacketFrame(
				Configuration::Serverbound::
					SelectKnownPacksPacketId,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeFinishConfigurationFrame() {
			return EncodePacketFrame(
				Configuration::Serverbound::
					FinishConfigurationPacketId,
				std::span<const std::byte>{});
		}

		[[nodiscard]]
		ByteBuffer MakeByteBuffer(
			std::span<const std::byte> bytes) {

			ByteBuffer result;
			result.WriteBytes(bytes);
			return result;
		}

		class LoopbackClient final {
		private:
			enum class WaitResult : std::uint8_t {
				Ready,
				TimedOut,
				Failed,
			};

		public:
			LoopbackClient() noexcept {
				WSADATA data{};

				const int result =
					WSAStartup(
						MAKEWORD(2, 2),
						&data);

				if (result != 0) {
					m_LastError = result;
					return;
				}

				if (LOBYTE(data.wVersion) != 2
					|| HIBYTE(data.wVersion) != 2) {

					m_LastError =
						WSAVERNOTSUPPORTED;

					WSACleanup();
					return;
				}

				m_WinsockInitialized = true;
			}

			~LoopbackClient() noexcept {
				Close();

				if (m_WinsockInitialized)
					WSACleanup();
			}

			LoopbackClient(
				const LoopbackClient&) = delete;

			LoopbackClient& operator=(
				const LoopbackClient&) = delete;

			LoopbackClient(
				LoopbackClient&&) = delete;

			LoopbackClient& operator=(
				LoopbackClient&&) = delete;

			[[nodiscard]]
			bool IsReady() const noexcept {
				return m_WinsockInitialized;
			}

			[[nodiscard]]
			bool IsConnected() const noexcept {
				return m_Socket != INVALID_SOCKET;
			}

			[[nodiscard]]
			int GetLastError() const noexcept {
				return m_LastError;
			}

			[[nodiscard]]
			bool Connect(
				std::string_view address,
				std::uint16_t port,
				std::chrono::milliseconds timeout =
					SocketTimeout) {

				if (!m_WinsockInitialized)
					return false;

				Close();

				m_Socket = socket(
					AF_INET,
					SOCK_STREAM,
					IPPROTO_TCP);

				if (m_Socket == INVALID_SOCKET) {
					m_LastError = WSAGetLastError();
					return false;
				}

				u_long nonblocking{ 1 };

				if (ioctlsocket(
					m_Socket,
					FIONBIO,
					&nonblocking) == SOCKET_ERROR) {

					m_LastError = WSAGetLastError();
					Close();
					return false;
				}

				sockaddr_in endpoint{};

				endpoint.sin_family = AF_INET;
				endpoint.sin_port = htons(port);

				const std::string address_string(
					address);

				if (inet_pton(
					AF_INET,
					address_string.c_str(),
					&endpoint.sin_addr) != 1) {

					m_LastError = WSAEINVAL;
					Close();
					return false;
				}

				const int connect_result = connect(
					m_Socket,
					reinterpret_cast<
						const sockaddr*>(&endpoint),
					static_cast<int>(
						sizeof(endpoint)));

				if (connect_result == SOCKET_ERROR) {
					const int error =
						WSAGetLastError();

					if (error != WSAEWOULDBLOCK
						&& error != WSAEINPROGRESS
						&& error != WSAEALREADY) {

						m_LastError = error;
						Close();
						return false;
					}

					if (WaitForSocket(
						false,
						true,
						timeout)
						!= WaitResult::Ready) {

						Close();
						return false;
					}

					int socket_error{ 0 };

					int socket_error_size{
						static_cast<int>(
							sizeof(socket_error))
					};

					if (getsockopt(
						m_Socket,
						SOL_SOCKET,
						SO_ERROR,
						reinterpret_cast<char*>(
							&socket_error),
						&socket_error_size)
						== SOCKET_ERROR) {

						m_LastError =
							WSAGetLastError();

						Close();
						return false;
					}

					if (socket_error != 0) {
						m_LastError = socket_error;
						Close();
						return false;
					}
				}

				const BOOL no_delay = TRUE;

				if (setsockopt(
					m_Socket,
					IPPROTO_TCP,
					TCP_NODELAY,
					reinterpret_cast<
						const char*>(&no_delay),
					static_cast<int>(
						sizeof(no_delay)))
					== SOCKET_ERROR) {

					m_LastError = WSAGetLastError();
					Close();
					return false;
				}

				m_LastError = 0;
				return true;
			}

			[[nodiscard]]
			bool SendAll(
				std::span<const std::byte> data,
				std::chrono::milliseconds timeout =
					SocketTimeout) {

				if (!IsConnected()) {
					m_LastError = WSAENOTCONN;
					return false;
				}

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				std::size_t offset{ 0 };

				while (offset < data.size()) {
					const auto remaining_time =
						RemainingUntil(deadline);

					if (WaitForSocket(
						false,
						true,
						remaining_time)
						!= WaitResult::Ready) {

						return false;
					}

					const std::size_t remaining =
						data.size() - offset;

					const int request_size =
						static_cast<int>(
							std::min(
								remaining,
								static_cast<
									std::size_t>(
										std::numeric_limits<
											int>::max())));

					const int sent = send(
						m_Socket,
						reinterpret_cast<
							const char*>(
								data.data() + offset),
						request_size,
						0);

					if (sent > 0) {
						offset +=
							static_cast<std::size_t>(
								sent);

						continue;
					}

					if (sent == 0) {
						m_LastError =
							WSAECONNRESET;

						return false;
					}

					const int error =
						WSAGetLastError();

					if (error == WSAEWOULDBLOCK) {
						if (
							std::chrono::
								steady_clock::now()
							>= deadline) {

							m_LastError =
								WSAETIMEDOUT;

							return false;
						}

						continue;
					}

					m_LastError = error;
					return false;
				}

				m_LastError = 0;
				return true;
			}

			/*
				Returns:
					nullopt      timeout or socket error
					empty vector orderly peer shutdown
					data vector  received bytes
			*/
			[[nodiscard]]
			std::optional<std::vector<std::byte>>
			ReceiveSome(
				std::size_t maximum_bytes,
				std::chrono::milliseconds timeout) {

				if (!IsConnected()) {
					m_LastError = WSAENOTCONN;
					return std::nullopt;
				}

				if (maximum_bytes == 0)
					return std::vector<std::byte>{};

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (true) {
					const auto remaining_time =
						RemainingUntil(deadline);

					if (WaitForSocket(
						true,
						false,
						remaining_time)
						!= WaitResult::Ready) {

						return std::nullopt;
					}

					const int request_size =
						static_cast<int>(
							std::min(
								maximum_bytes,
								static_cast<
									std::size_t>(
										std::numeric_limits<
											int>::max())));

					std::vector<std::byte> result(
						static_cast<std::size_t>(
							request_size));

					const int received = recv(
						m_Socket,
						reinterpret_cast<char*>(
							result.data()),
						request_size,
						0);

					if (received > 0) {
						result.resize(
							static_cast<std::size_t>(
								received));

						m_LastError = 0;
						return result;
					}

					if (received == 0) {
						result.clear();
						m_LastError = 0;
						return result;
					}

					const int error =
						WSAGetLastError();

					if (error == WSAEWOULDBLOCK) {
						if (
							std::chrono::
								steady_clock::now()
							>= deadline) {

							m_LastError =
								WSAETIMEDOUT;

							return std::nullopt;
						}

						continue;
					}

					m_LastError = error;
					return std::nullopt;
				}
			}

			[[nodiscard]]
			std::optional<std::vector<std::byte>>
			ReceiveExact(
				std::size_t size,
				std::chrono::milliseconds timeout =
					SocketTimeout) {

				std::vector<std::byte> result;
				result.reserve(size);

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (result.size() < size) {
					auto received = ReceiveSome(
						size - result.size(),
						RemainingUntil(deadline));

					if (!received.has_value())
						return std::nullopt;

					if (received->empty()) {
						m_LastError =
							WSAECONNRESET;

						return std::nullopt;
					}

					result.insert(
						result.end(),
						received->begin(),
						received->end());
				}

				return result;
			}

			[[nodiscard]]
			bool WaitForEof(
				std::chrono::milliseconds timeout =
					SocketTimeout) {

				auto received = ReceiveSome(
					ReceiveChunkSize,
					timeout);

				if (!received.has_value())
					return false;

				if (!received->empty()) {
					/*
						The caller should consume all
						expected application data before
						waiting for EOF.
					*/
					m_LastError = WSAEMSGSIZE;
					return false;
				}

				return true;
			}

			[[nodiscard]]
			bool WaitForDisconnect(
				std::chrono::milliseconds timeout =
					SocketTimeout) {

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (true) {
					auto received = ReceiveSome(
						ReceiveChunkSize,
						RemainingUntil(deadline));

					if (received.has_value()) {
						if (received->empty())
							return true;

						/*
							Discard any data already
							queued before termination.
						*/
						continue;
					}

					if (m_LastError == WSAECONNRESET
						|| m_LastError
							== WSAECONNABORTED
						|| m_LastError
							== WSAENOTCONN
						|| m_LastError
							== WSAESHUTDOWN) {

						return true;
					}

					return false;
				}
			}

			void Close() noexcept {
				if (m_Socket == INVALID_SOCKET)
					return;

				[[maybe_unused]]
				const int shutdown_result =
					shutdown(
						m_Socket,
						SD_BOTH);

				closesocket(m_Socket);
				m_Socket = INVALID_SOCKET;
			}

		private:
			[[nodiscard]]
			WaitResult WaitForSocket(
				bool readable,
				bool writable,
				std::chrono::milliseconds timeout) {

				if (m_Socket == INVALID_SOCKET) {
					m_LastError = WSAENOTCONN;
					return WaitResult::Failed;
				}

				fd_set read_set;
				fd_set write_set;
				fd_set exception_set;

				FD_ZERO(&read_set);
				FD_ZERO(&write_set);
				FD_ZERO(&exception_set);

				if (readable)
					FD_SET(m_Socket, &read_set);

				if (writable)
					FD_SET(m_Socket, &write_set);

				FD_SET(m_Socket, &exception_set);

				timeval timeval_value =
					MakeTimeval(timeout);

				const int result = select(
					0,
					readable
						? &read_set
						: nullptr,
					writable
						? &write_set
						: nullptr,
					&exception_set,
					&timeval_value);

				if (result == 0) {
					m_LastError = WSAETIMEDOUT;
					return WaitResult::TimedOut;
				}

				if (result == SOCKET_ERROR) {
					m_LastError = WSAGetLastError();
					return WaitResult::Failed;
				}

				if (FD_ISSET(
					m_Socket,
					&exception_set)) {

					int socket_error{ 0 };

					int socket_error_size{
						static_cast<int>(
							sizeof(socket_error))
					};

					if (getsockopt(
						m_Socket,
						SOL_SOCKET,
						SO_ERROR,
						reinterpret_cast<char*>(
							&socket_error),
						&socket_error_size)
						== SOCKET_ERROR) {

						m_LastError =
							WSAGetLastError();
					}
					else {
						m_LastError =
							socket_error != 0
							? socket_error
							: WSAECONNABORTED;
					}

					return WaitResult::Failed;
				}

				return WaitResult::Ready;
			}

			bool m_WinsockInitialized{ false };
			SOCKET m_Socket{ INVALID_SOCKET };
			int m_LastError{ 0 };
		};

		class LoopbackFrameReader final {
		public:
			void Append(
				std::span<const std::byte> bytes) {

				m_Decoder.Append(bytes);
			}

			[[nodiscard]]
			std::optional<PacketFrame> TryTake() {
				if (m_Error.has_value())
					return std::nullopt;

				auto decoded =
					m_Decoder.TryDecode();

				if (!decoded.has_value()) {
					m_Error = decoded.error();
					return std::nullopt;
				}

				if (!decoded->has_value())
					return std::nullopt;

				return std::move(
					decoded->value());
			}

			[[nodiscard]]
			bool HasError() const noexcept {
				return m_Error.has_value();
			}

			[[nodiscard]]
			std::optional<FrameError>
			GetError() const noexcept {

				return m_Error;
			}

			[[nodiscard]]
			std::size_t BufferedBytes() const noexcept {
				return m_Decoder.BufferedBytes();
			}

		private:
			PacketFrameDecoder m_Decoder;
			std::optional<FrameError> m_Error;
		};

		class NetworkEventCollector final {
		public:
			explicit NetworkEventCollector(
				NetworkManager& manager) noexcept
				: m_Manager(manager) {}

			template<typename TEvent>
			[[nodiscard]]
			std::optional<TEvent> WaitFor(
				std::chrono::milliseconds timeout =
					EventTimeout) {

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (true) {
					if (auto event =
						Extract<TEvent>()) {

						return event;
					}

					Drain();

					if (auto event =
						Extract<TEvent>()) {

						return event;
					}

					if (
						std::chrono::
							steady_clock::now()
						>= deadline) {

						return std::nullopt;
					}

					std::this_thread::sleep_for(
						PollInterval);
				}
			}

			void ExpectNoFailures() {
				Drain();

				for (const auto& event : m_Pending) {
					const auto* failure =
						std::get_if<
							NetworkFailureEvent>(
								&event);

					if (failure == nullptr)
						continue;

					ADD_FAILURE()
						<< "Unexpected network failure: "
						<< failure->Message
						<< " (fatal="
						<< failure->Fatal
						<< ')';
				}
			}

		private:
			void Drain() {
				auto events =
					m_Manager.DrainEvents();

				for (auto& event : events) {
					if (const auto* failure =
						std::get_if<
							NetworkFailureEvent>(
								&event)) {

						ADD_FAILURE()
							<< "Unexpected network "
							<< "failure: "
							<< failure->Message
							<< " (fatal="
							<< failure->Fatal
							<< ')';
					}

					m_Pending.push_back(
						std::move(event));
				}
			}

			template<typename TEvent>
			[[nodiscard]]
			std::optional<TEvent> Extract() {
				for (auto iterator =
					m_Pending.begin();
					iterator != m_Pending.end();
					++iterator) {

					auto* event =
						std::get_if<TEvent>(
							&*iterator);

					if (event == nullptr)
						continue;

					TEvent result =
						std::move(*event);

					m_Pending.erase(iterator);
					return result;
				}

				return std::nullopt;
			}

			NetworkManager& m_Manager;
			std::deque<NetworkEvent> m_Pending;
		};

		void ExpectDefaultStatusResponse(
			const PacketFrame& frame) {

			ASSERT_EQ(frame.PacketId, 0x00);

			ByteReader reader(
				PayloadBytes(frame));

			const auto json =
				reader.ReadString(
					MaximumStatusJsonBytes);

			ASSERT_TRUE(json.has_value());
			EXPECT_TRUE(reader.Empty());

			EXPECT_EQ(
				*json,
				R"({"version":{"name":"Aurore","protocol":774},"players":{"max":20,"online":0},"description":{"text":"Aurore Testing Server"}})");
		}

		void ExpectPong(
			const PacketFrame& frame,
			std::int64_t expected_value) {

			ASSERT_EQ(frame.PacketId, 0x01);

			ByteReader reader(
				PayloadBytes(frame));

			const auto value =
				reader.ReadInt64();

			ASSERT_TRUE(value.has_value());
			EXPECT_EQ(*value, expected_value);
			EXPECT_TRUE(reader.Empty());
		}

		void ExpectLoginSuccess(
			const PacketFrame& frame,
			std::string_view expected_username) {

			ASSERT_EQ(
				frame.PacketId,
				Login::Clientbound::
					SuccessPacketId);

			ByteReader reader(
				PayloadBytes(frame));

			const auto profile_id =
				reader.ReadUuid();

			ASSERT_TRUE(profile_id.has_value());
			EXPECT_FALSE(profile_id->IsNil());

			const auto username =
				reader.ReadString(
					Login::Serverbound::
						MaximumUsernameEncodedBytes);

			ASSERT_TRUE(username.has_value());
			EXPECT_EQ(*username, expected_username);

			const auto property_count =
				reader.ReadVarInt();

			ASSERT_TRUE(
				property_count.has_value());

			EXPECT_EQ(*property_count, 0);
			EXPECT_TRUE(reader.Empty());
		}

		void ExpectSyntheticFeatureFlags(
			const PacketFrame& frame) {

			ASSERT_EQ(
				frame.PacketId,
				Configuration::Clientbound::
					FeatureFlagsPacketId);

			ByteReader reader(
				PayloadBytes(frame));

			const auto feature_count =
				reader.ReadVarInt();

			ASSERT_TRUE(
				feature_count.has_value());

			ASSERT_EQ(*feature_count, 1);

			const auto feature =
				reader.ReadString(
					Configuration::DefaultLimits
						.MaximumIdentifierEncodedBytes);

			ASSERT_TRUE(feature.has_value());

			EXPECT_EQ(
				*feature,
				"minecraft:vanilla");

			EXPECT_TRUE(reader.Empty());
		}

		void ExpectEmptyKnownPackOffer(
			const PacketFrame& frame) {

			ASSERT_EQ(
				frame.PacketId,
				Configuration::Clientbound::
					SelectKnownPacksPacketId);

			ByteReader reader(
				PayloadBytes(frame));

			const auto pack_count =
				reader.ReadVarInt();

			ASSERT_TRUE(pack_count.has_value());
			EXPECT_EQ(*pack_count, 0);
			EXPECT_TRUE(reader.Empty());
		}

		class RawTransportLoopbackTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				ASSERT_TRUE(
					m_Manager.Initialize(
						MakeLoopbackConfig())
					.has_value());

				const auto start =
					m_Manager.Start();

				ASSERT_TRUE(start.has_value());
				ASSERT_NE(start->Port, 0);

				m_Endpoint = *start;
			}

			void TearDown() override {
				if (m_Manager.IsInitialized())
					m_Manager.Shutdown();

				const auto resources =
					NetworkManagerTestAccess::
						GetResourceSnapshot(
							m_Manager);

				EXPECT_EQ(
					resources.TrackedConnections,
					0);

				EXPECT_EQ(
					resources.ActiveConnections,
					0);

				EXPECT_EQ(
					resources.TotalOutboundBytes,
					0);

				EXPECT_EQ(
					resources.TotalInboundEventBytes,
					0);
			}

			NetworkManager m_Manager;

			NetworkEventCollector m_Events{
				m_Manager
			};

			NetworkEndpoint m_Endpoint;
		};

		TEST_F(
			RawTransportLoopbackTests,
			TransfersBytesBidirectionallyAndClosesAfterFlush) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady())
				<< client.GetLastError();

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port))
				<< client.GetLastError();

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			EXPECT_EQ(
				opened->LocalEndpoint.Port,
				m_Endpoint.Port);

			const auto client_payload =
				MakeBytes("client-to-server");

			ASSERT_TRUE(
				client.SendAll(client_payload))
				<< client.GetLastError();

			const auto received =
				m_Events.WaitFor<
					BytesReceivedEvent>();

			ASSERT_TRUE(received.has_value());

			EXPECT_EQ(
				received->Connection,
				opened->Connection);

			EXPECT_EQ(
				received->Data,
				client_payload);

			ASSERT_TRUE(
				m_Manager.ResumeReceive(
					opened->Connection)
				.has_value());

			const auto server_payload =
				MakeBytes("server-to-client");

			ASSERT_TRUE(
				m_Manager.QueueSend(
					opened->Connection,
					MakeByteBuffer(
						server_payload))
				.has_value());

			const auto client_received =
				client.ReceiveExact(
					server_payload.size());

			ASSERT_TRUE(
				client_received.has_value())
				<< client.GetLastError();

			EXPECT_EQ(
				*client_received,
				server_payload);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(
					opened->Connection)
				.has_value());

			EXPECT_TRUE(client.WaitForEof())
				<< client.GetLastError();

			const auto closed =
				m_Events.WaitFor<
					ConnectionClosedEvent>();

			ASSERT_TRUE(closed.has_value());

			EXPECT_EQ(
				closed->Connection,
				opened->Connection);

			EXPECT_EQ(
				closed->Reason,
				ConnectionCloseReason::
					ApplicationRequested);

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			RemoteCloseAfterAcceptProducesOneCloseEvent) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady());

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port));

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			client.Close();

			const auto closed =
				m_Events.WaitFor<
					ConnectionClosedEvent>();

			ASSERT_TRUE(closed.has_value());

			EXPECT_EQ(
				closed->Connection,
				opened->Connection);

			EXPECT_EQ(
				closed->Reason,
				ConnectionCloseReason::
					RemoteClosed);

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			RemoteCloseWhileReceiveIsPendingRetiresConnection) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady());

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port));

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			const auto partial_payload =
				MakeBytes("partial");

			ASSERT_TRUE(
				client.SendAll(partial_payload));

			const auto received =
				m_Events.WaitFor<
					BytesReceivedEvent>();

			ASSERT_TRUE(received.has_value());

			EXPECT_EQ(
				received->Data,
				partial_payload);

			ASSERT_TRUE(
				m_Manager.ResumeReceive(
					opened->Connection)
				.has_value());

			/*
				The ResumeReceive command may still be
				queued when the client closes. TCP retains
				the FIN until the backend posts the next
				receive operation.
			*/
			client.Close();

			const auto closed =
				m_Events.WaitFor<
					ConnectionClosedEvent>();

			ASSERT_TRUE(closed.has_value());

			EXPECT_EQ(
				closed->Connection,
				opened->Connection);

			EXPECT_EQ(
				closed->Reason,
				ConnectionCloseReason::
					RemoteClosed);

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			StopWithActiveConnectionDisconnectsClient) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady());

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port));

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			m_Manager.Stop();

			EXPECT_TRUE(
				client.WaitForDisconnect())
				<< client.GetLastError();

			const auto closed =
				m_Events.WaitFor<
					ConnectionClosedEvent>();

			ASSERT_TRUE(closed.has_value());

			EXPECT_EQ(
				closed->Connection,
				opened->Connection);

			EXPECT_EQ(
				closed->Reason,
				ConnectionCloseReason::
					ServerStopping);

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			InboundReservationRemainsUntilEventsAreDrained) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady())
				<< client.GetLastError();

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port))
				<< client.GetLastError();

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			const auto payload =
				std::vector<std::byte>(
					1024,
					std::byte{ 0x3C });

			ASSERT_TRUE(
				client.SendAll(payload))
				<< client.GetLastError();

			const auto deadline =
				std::chrono::steady_clock::now()
				+ EventTimeout;

			/*
				The resource reservation is acquired immediately before the
				BytesReceivedEvent is pushed. Observing the reservation alone does
				not prove that the event has reached the queue yet.

				Wait for the queue entry first. Once queued, no consumer can remove
				it until this test explicitly calls NetworkEventCollector::WaitFor.
			*/
			auto event_queue =
				NetworkManagerTestAccess::
				GetEventQueueSnapshot(
					m_Manager);

			do {
				event_queue =
					NetworkManagerTestAccess::
					GetEventQueueSnapshot(
						m_Manager);

				if (event_queue.Entries == 1
					&& event_queue.Bytes == payload.size()) {

					break;
				}

				std::this_thread::sleep_for(
					PollInterval);

			} while (
				std::chrono::steady_clock::now()
				< deadline);

			ASSERT_EQ(event_queue.Entries, 1);
			ASSERT_EQ(
				event_queue.Bytes,
				payload.size());

			/*
				The queued event owns the inbound reservation. Since the event has
				not been drained yet, its reservation must still be reflected in the
				resource ledger.
			*/
			const auto resources =
				NetworkManagerTestAccess::
				GetResourceSnapshot(
					m_Manager);

			EXPECT_EQ(
				resources.TotalInboundEventBytes,
				payload.size());

			const auto received =
				m_Events.WaitFor<
					BytesReceivedEvent>();

			ASSERT_TRUE(received.has_value());

			EXPECT_EQ(
				received->Connection,
				opened->Connection);

			EXPECT_EQ(
				received->Data,
				payload);

			EXPECT_EQ(
				NetworkManagerTestAccess::
					GetResourceSnapshot(
						m_Manager)
					.TotalInboundEventBytes,
				0);

			EXPECT_EQ(
				NetworkManagerTestAccess::
					GetEventQueueSnapshot(
						m_Manager)
					.Entries,
				0);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(
					opened->Connection)
				.has_value());

			EXPECT_TRUE(client.WaitForEof())
				<< client.GetLastError();

			ASSERT_TRUE(
				m_Events.WaitFor<
					ConnectionClosedEvent>()
				.has_value());

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			OutboundReservationReleasesAfterTransferCompletes) {

			LoopbackClient client;

			ASSERT_TRUE(client.IsReady())
				<< client.GetLastError();

			ASSERT_TRUE(
				client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port))
				<< client.GetLastError();

			const auto opened =
				m_Events.WaitFor<
					ConnectionOpenedEvent>();

			ASSERT_TRUE(opened.has_value());

			const auto payload =
				std::vector<std::byte>(
					256 * 1024,
					std::byte{ 0x6D });

			ASSERT_TRUE(
				m_Manager.QueueSend(
					opened->Connection,
					MakeByteBuffer(payload))
				.has_value());

			const auto received =
				client.ReceiveExact(
					payload.size());

			ASSERT_TRUE(received.has_value())
				<< client.GetLastError();

			EXPECT_EQ(*received, payload);

			const auto deadline =
				std::chrono::steady_clock::now()
				+ EventTimeout;

			while (
				NetworkManagerTestAccess::
					GetResourceSnapshot(
						m_Manager)
					.TotalOutboundBytes != 0
				&& std::chrono::steady_clock::now()
					< deadline) {

				std::this_thread::sleep_for(
					PollInterval);
			}

			EXPECT_EQ(
				NetworkManagerTestAccess::
					GetResourceSnapshot(
						m_Manager)
					.TotalOutboundBytes,
				0);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(
					opened->Connection)
				.has_value());

			EXPECT_TRUE(client.WaitForEof())
				<< client.GetLastError();

			ASSERT_TRUE(
				m_Events.WaitFor<
					ConnectionClosedEvent>()
				.has_value());

			m_Events.ExpectNoFailures();
		}

		class ServerLoopbackTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				ASSERT_TRUE(
					ServerTestAccess::
						InitializeDataFoundations(
							m_Server));

				ASSERT_TRUE(
					ServerTestAccess::
						InitializeProtocolConfiguration(
							m_Server));

				ASSERT_TRUE(
					ServerTestAccess::
						InitializeClientManagement(
							m_Server));

				m_Manager =
					&ServerTestAccess::
						GetNetworkManager(
							m_Server);

				ASSERT_TRUE(
					m_Manager->Initialize(
						MakeLoopbackConfig())
					.has_value());

				const auto start =
					m_Manager->Start();

				ASSERT_TRUE(start.has_value());
				ASSERT_NE(start->Port, 0);

				m_Endpoint = *start;
			}

			void TearDown() override {
				if (
					m_Manager != nullptr
					&& m_Manager->IsInitialized()) {

					m_Manager->Shutdown();
				}

				if (m_Manager == nullptr)
					return;

				const auto resources =
					NetworkManagerTestAccess::
						GetResourceSnapshot(
							*m_Manager);

				EXPECT_EQ(
					resources.TrackedConnections,
					0);

				EXPECT_EQ(
					resources.ActiveConnections,
					0);

				EXPECT_EQ(
					resources.TotalOutboundBytes,
					0);

				EXPECT_EQ(
					resources.TotalInboundEventBytes,
					0);
			}

			template<typename TPredicate>
			[[nodiscard]]
			bool PumpUntil(
				TPredicate&& predicate,
				std::chrono::milliseconds timeout =
					EventTimeout) {

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (true) {
					ServerTestAccess::
						ProcessNetworkEvents(
							m_Server);

					if (std::forward<TPredicate>(
						predicate)()) {

						return true;
					}

					if (
						std::chrono::
							steady_clock::now()
						>= deadline) {

						return false;
					}

					std::this_thread::sleep_for(
						PollInterval);
				}
			}

			void PumpFor(
				std::chrono::milliseconds duration) {

				const auto deadline =
					std::chrono::steady_clock::now()
					+ duration;

				while (
					std::chrono::steady_clock::now()
					< deadline) {

					ServerTestAccess::
						ProcessNetworkEvents(
							m_Server);

					std::this_thread::sleep_for(
						PollInterval);
				}
			}

			[[nodiscard]]
			bool WaitForProtocolConnectionCount(
				std::size_t expected,
				std::chrono::milliseconds timeout =
					EventTimeout) {

				return PumpUntil(
					[this, expected] {
						return ServerTestAccess::
							GetConnectionCount(
								m_Server)
							== expected;
					},
					timeout);
			}

			[[nodiscard]]
			std::optional<PacketFrame> ReceiveFrame(
				LoopbackClient& client,
				LoopbackFrameReader& reader,
				std::chrono::milliseconds timeout =
					EventTimeout) {

				const auto deadline =
					std::chrono::steady_clock::now()
					+ timeout;

				while (true) {
					ServerTestAccess::
						ProcessNetworkEvents(
							m_Server);

					if (auto frame =
						reader.TryTake()) {

						return frame;
					}

					if (reader.HasError()) {
						ADD_FAILURE()
							<< "Loopback frame "
							<< "decoder failed";

						return std::nullopt;
					}

					auto received =
						client.ReceiveSome(
							ReceiveChunkSize,
							0ms);

					if (received.has_value()) {
						if (received->empty()) {
							ADD_FAILURE()
								<< "Connection closed "
								<< "while waiting for "
								<< "a packet frame";

							return std::nullopt;
						}

						reader.Append(*received);

						if (auto frame =
							reader.TryTake()) {

							return frame;
						}

						if (reader.HasError()) {
							ADD_FAILURE()
								<< "Loopback frame "
								<< "decoder failed";

							return std::nullopt;
						}
					}
					else if (
						client.GetLastError()
							!= WSAETIMEDOUT
						&& client.GetLastError()
							!= WSAEWOULDBLOCK) {

						ADD_FAILURE()
							<< "Socket receive failed "
							<< "with "
							<< client.GetLastError();

						return std::nullopt;
					}

					if (
						std::chrono::
							steady_clock::now()
						>= deadline) {

						return std::nullopt;
					}

					std::this_thread::sleep_for(
						PollInterval);
				}
			}

			[[nodiscard]]
			bool ConnectClient(
				LoopbackClient& client,
				std::size_t expected_connection_count) {

				if (!client.IsReady())
					return false;

				if (!client.Connect(
					m_Endpoint.Address,
					m_Endpoint.Port)) {

					return false;
				}

				return WaitForProtocolConnectionCount(
					expected_connection_count);
			}

			Server m_Server;
			NetworkManager* m_Manager{ nullptr };
			NetworkEndpoint m_Endpoint;
		};

		TEST_F(
			ServerLoopbackTests,
			CompletesHandshakeStatusPingAndPong) {

			constexpr std::int64_t PingValue{
				123456789012345LL
			};

			LoopbackClient client;

			ASSERT_TRUE(
				ConnectClient(client, 1))
				<< client.GetLastError();

			const auto handshake =
				MakeStatusHandshakeFrame(
					m_Endpoint.Port);

			const auto status_request =
				MakeStatusRequestFrame();

			ASSERT_TRUE(
				client.SendAll(handshake.Bytes()));

			PumpFor(10ms);

			ASSERT_TRUE(
				client.SendAll(
					status_request.Bytes()));

			LoopbackFrameReader reader;

			const auto status_response =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				status_response.has_value());

			ExpectDefaultStatusResponse(
				*status_response);

			const auto ping_request =
				MakePingRequestFrame(
					PingValue);

			ASSERT_TRUE(
				client.SendAll(
					ping_request.Bytes()));

			const auto pong =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof())
				<< client.GetLastError();

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			HandlesEntireStatusSequenceFragmentedOneByteAtATime) {

			constexpr std::int64_t PingValue{
				0x102030405060708LL
			};

			LoopbackClient client;

			ASSERT_TRUE(
				ConnectClient(client, 1));

			const auto handshake =
				MakeStatusHandshakeFrame(
					m_Endpoint.Port);

			const auto status_request =
				MakeStatusRequestFrame();

			const auto ping_request =
				MakePingRequestFrame(
					PingValue);

			const auto request =
				Combine({
					handshake.Bytes(),
					status_request.Bytes(),
					ping_request.Bytes(),
				});

			for (std::size_t index{ 0 };
				index < request.size();
				++index) {

				const std::span<const std::byte>
					single_byte(
						request.data() + index,
						1);

				ASSERT_TRUE(
					client.SendAll(single_byte));

				ServerTestAccess::
					ProcessNetworkEvents(
						m_Server);

				std::this_thread::sleep_for(
					PollInterval);
			}

			LoopbackFrameReader reader;

			const auto status_response =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				status_response.has_value());

			ExpectDefaultStatusResponse(
				*status_response);

			const auto pong =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			HandlesCoalescedHandshakeAndStatusRequest) {

			constexpr std::int64_t PingValue{ 55 };

			LoopbackClient client;

			ASSERT_TRUE(
				ConnectClient(client, 1));

			const auto handshake =
				MakeStatusHandshakeFrame(
					m_Endpoint.Port);

			const auto status_request =
				MakeStatusRequestFrame();

			const auto first_write =
				Combine({
					handshake.Bytes(),
					status_request.Bytes(),
				});

			ASSERT_TRUE(
				client.SendAll(first_write));

			LoopbackFrameReader reader;

			const auto status_response =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				status_response.has_value());

			ExpectDefaultStatusResponse(
				*status_response);

			const auto ping_request =
				MakePingRequestFrame(
					PingValue);

			ASSERT_TRUE(
				client.SendAll(
					ping_request.Bytes()));

			const auto pong =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			HandlesHandshakeStatusRequestAndPingInSingleSend) {

			constexpr std::int64_t PingValue{
				987654321
			};

			LoopbackClient client;

			ASSERT_TRUE(
				ConnectClient(client, 1));

			const auto handshake =
				MakeStatusHandshakeFrame(
					m_Endpoint.Port);

			const auto status_request =
				MakeStatusRequestFrame();

			const auto ping_request =
				MakePingRequestFrame(
					PingValue);

			const auto request =
				Combine({
					handshake.Bytes(),
					status_request.Bytes(),
					ping_request.Bytes(),
				});

			ASSERT_TRUE(client.SendAll(request));

			LoopbackFrameReader reader;

			const auto status_response =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				status_response.has_value());

			ExpectDefaultStatusResponse(
				*status_response);

			const auto pong =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			SupportsMultipleSequentialStatusClients) {

			constexpr std::size_t ClientCount{ 8 };

			for (std::size_t index{ 0 };
				index < ClientCount;
				++index) {

				SCOPED_TRACE(
					::testing::Message()
					<< "Sequential client "
					<< index);

				LoopbackClient client;

				ASSERT_TRUE(
					ConnectClient(client, 1));

				const auto ping_value =
					static_cast<std::int64_t>(
						1000 + index);

				const auto handshake =
					MakeStatusHandshakeFrame(
						m_Endpoint.Port);

				const auto status_request =
					MakeStatusRequestFrame();

				const auto ping_request =
					MakePingRequestFrame(
						ping_value);

				const auto request =
					Combine({
						handshake.Bytes(),
						status_request.Bytes(),
						ping_request.Bytes(),
					});

				ASSERT_TRUE(
					client.SendAll(request));

				LoopbackFrameReader reader;

				const auto status_response =
					ReceiveFrame(client, reader);

				ASSERT_TRUE(
					status_response.has_value());

				ExpectDefaultStatusResponse(
					*status_response);

				const auto pong =
					ReceiveFrame(client, reader);

				ASSERT_TRUE(pong.has_value());

				ExpectPong(
					*pong,
					ping_value);

				ASSERT_TRUE(
					client.WaitForEof());

				ASSERT_TRUE(
					WaitForProtocolConnectionCount(
						0));
			}
		}

		TEST_F(
			ServerLoopbackTests,
			SupportsMultipleConcurrentStatusClients) {

			constexpr std::size_t ClientCount{ 4 };

			std::vector<
				std::unique_ptr<LoopbackClient>>
				clients;

			clients.reserve(ClientCount);

			for (std::size_t index{ 0 };
				index < ClientCount;
				++index) {

				auto client =
					std::make_unique<
						LoopbackClient>();

				ASSERT_TRUE(client->IsReady());

				ASSERT_TRUE(
					client->Connect(
						m_Endpoint.Address,
						m_Endpoint.Port));

				clients.push_back(
					std::move(client));
			}

			ASSERT_TRUE(
				WaitForProtocolConnectionCount(
					ClientCount));

			std::vector<LoopbackFrameReader>
				readers(ClientCount);

			for (std::size_t index{ 0 };
				index < ClientCount;
				++index) {

				const auto ping_value =
					static_cast<std::int64_t>(
						5000 + index);

				const auto handshake =
					MakeStatusHandshakeFrame(
						m_Endpoint.Port);

				const auto status_request =
					MakeStatusRequestFrame();

				const auto ping_request =
					MakePingRequestFrame(
						ping_value);

				const auto request =
					Combine({
						handshake.Bytes(),
						status_request.Bytes(),
						ping_request.Bytes(),
					});

				ASSERT_TRUE(
					clients[index]->SendAll(
						request));
			}

			for (std::size_t index{ 0 };
				index < ClientCount;
				++index) {

				SCOPED_TRACE(
					::testing::Message()
					<< "Concurrent client "
					<< index);

				const auto ping_value =
					static_cast<std::int64_t>(
						5000 + index);

				const auto status_response =
					ReceiveFrame(
						*clients[index],
						readers[index]);

				ASSERT_TRUE(
					status_response.has_value());

				ExpectDefaultStatusResponse(
					*status_response);

				const auto pong =
					ReceiveFrame(
						*clients[index],
						readers[index]);

				ASSERT_TRUE(pong.has_value());

				ExpectPong(
					*pong,
					ping_value);
			}

			for (auto& client : clients)
				ASSERT_TRUE(client->WaitForEof());

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			CompletesLoginAndConfigurationThroughCoreDispatcher) {

			constexpr std::string_view Username{
				"PlayerOne"
			};

			LoopbackClient client;

			ASSERT_TRUE(
				ConnectClient(client, 1))
				<< client.GetLastError();

			const auto snapshots =
				ServerTestAccess::
					GetClientSnapshots(
						m_Server);

			ASSERT_EQ(snapshots.size(), 1u);

			const ConnectionId connection =
				snapshots.front().Connection;

			LoopbackFrameReader reader;

			const auto handshake =
				MakeLoginHandshakeFrame(
					m_Endpoint.Port);

			ASSERT_TRUE(
				client.SendAll(
					handshake.Bytes()))
				<< client.GetLastError();

			PumpFor(10ms);

			const auto login_start =
				MakeLoginStartFrame(Username);

			ASSERT_TRUE(
				client.SendAll(
					login_start.Bytes()))
				<< client.GetLastError();

			const auto login_success =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				login_success.has_value());

			ExpectLoginSuccess(
				*login_success,
				Username);

			/*
				Coalesce Login Acknowledged and Client
				Information. The Configuration bootstrap
				request must not act as a strict inbound
				decision boundary.
			*/
			const auto acknowledged =
				MakeLoginAcknowledgedFrame();

			const auto client_information =
				MakeClientInformationFrame();

			const auto enter_configuration =
				Combine({
					acknowledged.Bytes(),
					client_information.Bytes(),
				});

			ASSERT_TRUE(
				client.SendAll(
					enter_configuration))
				<< client.GetLastError();

			/*
				The real Core dispatcher must handle
				ConfigurationStartRequest, acquire the
				active snapshot, build the plan, resolve
				the session, and queue the initial phase.
			*/
			const auto feature_flags =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				feature_flags.has_value());

			ExpectSyntheticFeatureFlags(
				*feature_flags);

			const auto known_pack_offer =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				known_pack_offer.has_value());

			ExpectEmptyKnownPackOffer(
				*known_pack_offer);

			const auto known_pack_selection =
				MakeEmptyKnownPackSelectionFrame();

			ASSERT_TRUE(
				client.SendAll(
					known_pack_selection.Bytes()))
				<< client.GetLastError();

			const auto dimension_registry =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				dimension_registry.has_value());

			EXPECT_EQ(
				dimension_registry->PacketId,
				Configuration::Clientbound::
					RegistryDataPacketId);

			const auto biome_registry =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				biome_registry.has_value());

			EXPECT_EQ(
				biome_registry->PacketId,
				Configuration::Clientbound::
					RegistryDataPacketId);

			const auto tags =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(tags.has_value());

			EXPECT_EQ(
				tags->PacketId,
				Configuration::Clientbound::
					TagsPacketId);

			const auto finish_request =
				ReceiveFrame(client, reader);

			ASSERT_TRUE(
				finish_request.has_value());

			EXPECT_EQ(
				finish_request->PacketId,
				Configuration::Clientbound::
					FinishConfigurationPacketId);

			const auto finish_acknowledgement =
				MakeFinishConfigurationFrame();

			ASSERT_TRUE(
				client.SendAll(
					finish_acknowledgement.Bytes()))
				<< client.GetLastError();

			ASSERT_TRUE(
				PumpUntil(
					[this, connection] {
						const auto* server_client =
							ServerTestAccess::
								FindClient(
									m_Server,
									connection);

						return server_client
							!= nullptr
							&& server_client
								->GetProtocolState()
								== ProtocolState::Play;
					}));

			auto* server_client =
				ServerTestAccess::FindClient(
					m_Server,
					connection);

			ASSERT_NE(server_client, nullptr);

			EXPECT_EQ(
				server_client->GetProtocolState(),
				ProtocolState::Play);

			EXPECT_EQ(
				server_client
					->GetLifecycleState(),
				ClientLifecycleState::Play);

			ASSERT_TRUE(
				server_client->GetIdentity()
					.has_value());

			EXPECT_EQ(
				server_client->GetIdentity()
					->Username,
				Username);

			const auto& session =
				server_client
					->GetProtocolConnection()
					.GetSession();

			ASSERT_TRUE(
				session.GetRegistrySnapshot());

			EXPECT_EQ(
				session.GetConfigurationGeneration(),
				InitialSyntheticRegistryGeneration);

			EXPECT_EQ(
				session.GetRegistrySnapshot()
					->GetGeneration(),
				InitialSyntheticRegistryGeneration);

			client.Close();

			EXPECT_TRUE(
				WaitForProtocolConnectionCount(0));
		}
	}
}
