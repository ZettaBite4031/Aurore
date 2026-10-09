#include "NetworkLoopbackClient.hpp"

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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using namespace std::chrono_literals;

		namespace Configuration = Aurore::Protocol::Packets::Configuration;
		namespace Login = Aurore::Protocol::Packets::Login;

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
		constexpr auto PollInterval = 1ms;
		constexpr std::size_t ReceiveChunkSize = 64 * 1024;
		constexpr std::size_t MaximumStatusJsonBytes = 2 * 1024 * 1024;

		[[nodiscard]]
		NetworkConfiguration MakeLoopbackConfig() {
			return NetworkConfiguration{
				.Backend = NetworkBackendType::Automatic,
				.BindAddress = "127.0.0.1",
				.Port = 0,
				.MaximumConnections = 64,
				.ReceiveBufferSize = 4096,
				.MaximumInboundBytesPerConnection = 2 * 1024 * 1024,
				.MaximumOutboundBytesPerConnection = 2 * 1024 * 1024,
				.MaximumCommandQueueEntries = 1024,
				.MaximumCommandQueueBytes = 8 * 1024 * 1024,
				.MaximumEventQueueEntries = 1024,
				.MaximumEventQueueBytes = 8 * 1024 * 1024,
				.MaximumTotalOutboundBytes = 32 * 1024 * 1024,
				.MaximumTotalInboundEventBytes = 32 * 1024 * 1024,
			};
		}

		[[nodiscard]]
		std::vector<std::byte> MakeBytes(std::string_view text) {
			std::vector<std::byte> result;
			result.reserve(text.size());

			for (const char character : text) {
				result.push_back(
					static_cast<std::byte>(
						static_cast<unsigned char>(character)));
			}

			return result;
		}

		[[nodiscard]]
		std::vector<std::byte> Combine(
			std::initializer_list<std::span<const std::byte>> ranges) {

			std::size_t total_size{ 0 };
			for (const auto range : ranges)
				total_size += range.size();

			std::vector<std::byte> result;
			result.reserve(total_size);

			for (const auto range : ranges)
				result.insert(result.end(), range.begin(), range.end());

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
		ByteBuffer MakeByteBuffer(std::span<const std::byte> bytes) {
			ByteBuffer result;
			result.WriteBytes(bytes);
			return result;
		}

		[[nodiscard]]
		ByteBuffer MakeStatusHandshakeFrame(std::uint16_t port) {
			ByteBuffer payload;
			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(port);
			payload.WriteVarInt(
				static_cast<std::int32_t>(HandshakeIntention::Status));
			return EncodePacketFrame(0x00, payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeLoginHandshakeFrame(std::uint16_t port) {
			ByteBuffer payload;
			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(port);
			payload.WriteVarInt(
				static_cast<std::int32_t>(HandshakeIntention::Login));
			return EncodePacketFrame(0x00, payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeStatusRequestFrame() {
			return EncodePacketFrame(0x00, std::span<const std::byte>{});
		}

		[[nodiscard]]
		ByteBuffer MakePingRequestFrame(std::int64_t value) {
			ByteBuffer payload;
			payload.WriteInt64(value);
			return EncodePacketFrame(0x01, payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeLoginStartFrame(std::string_view username) {
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
				Login::Serverbound::AcknowledgedPacketId,
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
					Configuration::ParticleStatus::All));

			return EncodePacketFrame(
				Configuration::Serverbound::ClientInformationPacketId,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeEmptyKnownPackSelectionFrame() {
			ByteBuffer payload;
			payload.WriteVarInt(0);
			return EncodePacketFrame(
				Configuration::Serverbound::SelectKnownPacksPacketId,
				payload.Bytes());
		}

		[[nodiscard]]
		ByteBuffer MakeFinishConfigurationFrame() {
			return EncodePacketFrame(
				Configuration::Serverbound::FinishConfigurationPacketId,
				std::span<const std::byte>{});
		}

		class LoopbackFrameReader final {
		public:
			void Append(std::span<const std::byte> bytes) {
				m_Decoder.Append(bytes);
			}

			[[nodiscard]]
			std::optional<PacketFrame> TryTake() {
				if (m_Error.has_value())
					return std::nullopt;

				auto decoded = m_Decoder.TryDecode();
				if (!decoded.has_value()) {
					m_Error = decoded.error();
					return std::nullopt;
				}

				if (!decoded->has_value())
					return std::nullopt;

				return std::move(decoded->value());
			}

			[[nodiscard]] bool HasError() const noexcept {
				return m_Error.has_value();
			}

		private:
			PacketFrameDecoder m_Decoder;
			std::optional<FrameError> m_Error;
		};

		class NetworkEventCollector final {
		public:
			explicit NetworkEventCollector(NetworkManager& manager) noexcept
				: m_Manager(manager) {}

			template<typename TEvent>
			[[nodiscard]]
			std::optional<TEvent> WaitFor(
				std::chrono::milliseconds timeout = EventTimeout) {

				const auto deadline = std::chrono::steady_clock::now() + timeout;

				while (true) {
					if (auto event = Extract<TEvent>())
						return event;

					Drain();

					if (auto event = Extract<TEvent>())
						return event;

					if (std::chrono::steady_clock::now() >= deadline)
						return std::nullopt;

					std::this_thread::sleep_for(PollInterval);
				}
			}

			void ExpectNoFailures() {
				Drain();

				for (const auto& event : m_Pending) {
					const auto* failure =
						std::get_if<NetworkFailureEvent>(&event);

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
				auto events = m_Manager.DrainEvents();

				for (auto& event : events)
					m_Pending.push_back(std::move(event));
			}

			template<typename TEvent>
			[[nodiscard]]
			std::optional<TEvent> Extract() {
				for (auto iterator = m_Pending.begin();
					iterator != m_Pending.end();
					++iterator) {

					auto* event = std::get_if<TEvent>(&*iterator);
					if (event == nullptr)
						continue;

					TEvent result = std::move(*event);
					m_Pending.erase(iterator);
					return result;
				}

				return std::nullopt;
			}

			NetworkManager& m_Manager;
			std::deque<NetworkEvent> m_Pending;
		};

		void ExpectDefaultStatusResponse(const PacketFrame& frame) {
			ASSERT_EQ(frame.PacketId, 0x00);
			ByteReader reader(PayloadBytes(frame));
			const auto json = reader.ReadString(MaximumStatusJsonBytes);
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
			ByteReader reader(PayloadBytes(frame));
			const auto value = reader.ReadInt64();
			ASSERT_TRUE(value.has_value());
			EXPECT_EQ(*value, expected_value);
			EXPECT_TRUE(reader.Empty());
		}

		void ExpectLoginSuccess(
			const PacketFrame& frame,
			std::string_view expected_username) {

			ASSERT_EQ(frame.PacketId, Login::Clientbound::SuccessPacketId);
			ByteReader reader(PayloadBytes(frame));

			const auto profile_id = reader.ReadUuid();
			ASSERT_TRUE(profile_id.has_value());
			EXPECT_FALSE(profile_id->IsNil());

			const auto username = reader.ReadString(
				Login::Serverbound::MaximumUsernameEncodedBytes);
			ASSERT_TRUE(username.has_value());
			EXPECT_EQ(*username, expected_username);

			const auto property_count = reader.ReadVarInt();
			ASSERT_TRUE(property_count.has_value());
			EXPECT_EQ(*property_count, 0);
			EXPECT_TRUE(reader.Empty());
		}

		class RawTransportLoopbackTests : public ::testing::Test {
		protected:
			void SetUp() override {
				ASSERT_TRUE(m_Manager.Initialize(MakeLoopbackConfig()).has_value());
				const auto start = m_Manager.Start();
				ASSERT_TRUE(start.has_value());
				ASSERT_NE(start->Port, 0);
				m_Endpoint = *start;
			}

			void TearDown() override {
				if (m_Manager.IsInitialized())
					m_Manager.Shutdown();

				const auto resources =
					NetworkManagerTestAccess::GetResourceSnapshot(m_Manager);

				EXPECT_EQ(resources.TrackedConnections, 0);
				EXPECT_EQ(resources.ActiveConnections, 0);
				EXPECT_EQ(resources.TotalOutboundBytes, 0);
				EXPECT_EQ(resources.TotalInboundEventBytes, 0);
			}

			NetworkManager m_Manager;
			NetworkEventCollector m_Events{ m_Manager };
			NetworkEndpoint m_Endpoint;
		};

		TEST_F(
			RawTransportLoopbackTests,
			TransfersBytesBidirectionallyAndClosesAfterFlush) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady()) << client.GetLastError();
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port))
				<< client.GetLastError();

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());
			EXPECT_EQ(opened->LocalEndpoint.Port, m_Endpoint.Port);

			const auto client_payload = MakeBytes("client-to-server");
			ASSERT_TRUE(client.SendAll(client_payload)) << client.GetLastError();

			const auto received = m_Events.WaitFor<BytesReceivedEvent>();
			ASSERT_TRUE(received.has_value());
			EXPECT_EQ(received->Connection, opened->Connection);
			EXPECT_EQ(received->Data, client_payload);

			ASSERT_TRUE(
				m_Manager.ResumeReceive(opened->Connection).has_value());

			const auto server_payload = MakeBytes("server-to-client");
			ASSERT_TRUE(
				m_Manager.QueueSend(
					opened->Connection,
					MakeByteBuffer(server_payload))
				.has_value());

			const auto client_received =
				client.ReceiveExact(server_payload.size());
			ASSERT_TRUE(client_received.has_value()) << client.GetLastError();
			EXPECT_EQ(*client_received, server_payload);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(opened->Connection).has_value());
			EXPECT_TRUE(client.WaitForEof()) << client.GetLastError();

			const auto closed = m_Events.WaitFor<ConnectionClosedEvent>();
			ASSERT_TRUE(closed.has_value());
			EXPECT_EQ(closed->Connection, opened->Connection);
			EXPECT_EQ(
				closed->Reason,
				ConnectionCloseReason::ApplicationRequested);

			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			RemoteCloseAfterAcceptProducesOneCloseEvent) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady());
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port));

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());

			client.Close();

			const auto closed = m_Events.WaitFor<ConnectionClosedEvent>();
			ASSERT_TRUE(closed.has_value());
			EXPECT_EQ(closed->Connection, opened->Connection);
			EXPECT_EQ(closed->Reason, ConnectionCloseReason::RemoteClosed);
			m_Events.ExpectNoFailures();
		}


		TEST_F(
			RawTransportLoopbackTests,
			RemoteCloseWhileReceiveIsPendingRetiresConnection) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady());
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port));

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());

			const auto partial_payload = MakeBytes("partial");
			ASSERT_TRUE(client.SendAll(partial_payload));

			const auto received = m_Events.WaitFor<BytesReceivedEvent>();
			ASSERT_TRUE(received.has_value());
			EXPECT_EQ(received->Data, partial_payload);

			ASSERT_TRUE(
				m_Manager.ResumeReceive(opened->Connection).has_value());

			client.Close();

			const auto closed = m_Events.WaitFor<ConnectionClosedEvent>();
			ASSERT_TRUE(closed.has_value());
			EXPECT_EQ(closed->Connection, opened->Connection);
			EXPECT_EQ(closed->Reason, ConnectionCloseReason::RemoteClosed);
			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			StopWithActiveConnectionDisconnectsClient) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady());
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port));

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());

			m_Manager.Stop();

			EXPECT_TRUE(client.WaitForDisconnect()) << client.GetLastError();

			const auto closed = m_Events.WaitFor<ConnectionClosedEvent>();
			ASSERT_TRUE(closed.has_value());
			EXPECT_EQ(closed->Connection, opened->Connection);
			EXPECT_EQ(closed->Reason, ConnectionCloseReason::ServerStopping);
			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			InboundReservationRemainsUntilEventsAreDrained) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady());
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port));

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());

			const auto payload = std::vector<std::byte>(
				1024,
				std::byte{ 0x3C });

			ASSERT_TRUE(client.SendAll(payload));

			const auto deadline = std::chrono::steady_clock::now() + EventTimeout;
			auto event_queue =
				NetworkManagerTestAccess::GetEventQueueSnapshot(m_Manager);

			do {
				event_queue =
					NetworkManagerTestAccess::GetEventQueueSnapshot(m_Manager);

				if (event_queue.Entries == 1
					&& event_queue.Bytes == payload.size())
					break;

				std::this_thread::sleep_for(PollInterval);
			} while (std::chrono::steady_clock::now() < deadline);

			ASSERT_EQ(event_queue.Entries, 1);
			ASSERT_EQ(event_queue.Bytes, payload.size());

			EXPECT_EQ(
				NetworkManagerTestAccess::GetResourceSnapshot(m_Manager)
					.TotalInboundEventBytes,
				payload.size());

			const auto received = m_Events.WaitFor<BytesReceivedEvent>();
			ASSERT_TRUE(received.has_value());
			EXPECT_EQ(received->Connection, opened->Connection);
			EXPECT_EQ(received->Data, payload);

			EXPECT_EQ(
				NetworkManagerTestAccess::GetResourceSnapshot(m_Manager)
					.TotalInboundEventBytes,
				0);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(opened->Connection).has_value());
			EXPECT_TRUE(client.WaitForEof());
			ASSERT_TRUE(m_Events.WaitFor<ConnectionClosedEvent>().has_value());
			m_Events.ExpectNoFailures();
		}

		TEST_F(
			RawTransportLoopbackTests,
			OutboundReservationReleasesAfterTransferCompletes) {

			LoopbackClient client;
			ASSERT_TRUE(client.IsReady());
			ASSERT_TRUE(client.Connect(m_Endpoint.Address, m_Endpoint.Port));

			const auto opened = m_Events.WaitFor<ConnectionOpenedEvent>();
			ASSERT_TRUE(opened.has_value());

			const auto payload = std::vector<std::byte>(
				256 * 1024,
				std::byte{ 0x6D });

			ASSERT_TRUE(
				m_Manager.QueueSend(
					opened->Connection,
					MakeByteBuffer(payload))
				.has_value());

			const auto received = client.ReceiveExact(payload.size());
			ASSERT_TRUE(received.has_value()) << client.GetLastError();
			EXPECT_EQ(*received, payload);

			const auto deadline = std::chrono::steady_clock::now() + EventTimeout;
			while (
				NetworkManagerTestAccess::GetResourceSnapshot(m_Manager)
					.TotalOutboundBytes != 0
				&& std::chrono::steady_clock::now() < deadline) {

				std::this_thread::sleep_for(PollInterval);
			}

			EXPECT_EQ(
				NetworkManagerTestAccess::GetResourceSnapshot(m_Manager)
					.TotalOutboundBytes,
				0);

			ASSERT_TRUE(
				m_Manager.CloseAfterFlush(opened->Connection).has_value());
			EXPECT_TRUE(client.WaitForEof());
			ASSERT_TRUE(m_Events.WaitFor<ConnectionClosedEvent>().has_value());
			m_Events.ExpectNoFailures();
		}

		class ServerLoopbackTests : public ::testing::Test {
		protected:
			void SetUp() override {
				ASSERT_TRUE(ServerTestAccess::InitializeDataFoundations(m_Server));
				ASSERT_TRUE(ServerTestAccess::InitializeProtocolConfiguration(m_Server));
				ASSERT_TRUE(ServerTestAccess::InitializeClientManagement(m_Server));

				m_Manager = &ServerTestAccess::GetNetworkManager(m_Server);
				ASSERT_TRUE(m_Manager->Initialize(MakeLoopbackConfig()).has_value());

				const auto start = m_Manager->Start();
				ASSERT_TRUE(start.has_value());
				ASSERT_NE(start->Port, 0);
				m_Endpoint = *start;
			}

			void TearDown() override {
				if (m_Manager != nullptr && m_Manager->IsInitialized())
					m_Manager->Shutdown();

				if (m_Manager == nullptr)
					return;

				const auto resources =
					NetworkManagerTestAccess::GetResourceSnapshot(*m_Manager);

				EXPECT_EQ(resources.TrackedConnections, 0);
				EXPECT_EQ(resources.ActiveConnections, 0);
				EXPECT_EQ(resources.TotalOutboundBytes, 0);
				EXPECT_EQ(resources.TotalInboundEventBytes, 0);
			}

			template<typename TPredicate>
			[[nodiscard]]
			bool PumpUntil(
				TPredicate&& predicate,
				std::chrono::milliseconds timeout = EventTimeout) {

				const auto deadline = std::chrono::steady_clock::now() + timeout;

				while (true) {
					ServerTestAccess::ProcessNetworkEvents(m_Server);

					if (std::forward<TPredicate>(predicate)())
						return true;

					if (std::chrono::steady_clock::now() >= deadline)
						return false;

					std::this_thread::sleep_for(PollInterval);
				}
			}

			void PumpFor(std::chrono::milliseconds duration) {
				const auto deadline = std::chrono::steady_clock::now() + duration;

				while (std::chrono::steady_clock::now() < deadline) {
					ServerTestAccess::ProcessNetworkEvents(m_Server);
					std::this_thread::sleep_for(PollInterval);
				}
			}

			[[nodiscard]]
			bool WaitForProtocolConnectionCount(
				std::size_t expected,
				std::chrono::milliseconds timeout = EventTimeout) {

				return PumpUntil(
					[this, expected] {
						return ServerTestAccess::GetConnectionCount(m_Server)
							== expected;
					},
					timeout);
			}

			[[nodiscard]]
			std::optional<PacketFrame> ReceiveFrame(
				LoopbackClient& client,
				LoopbackFrameReader& reader,
				std::chrono::milliseconds timeout = EventTimeout) {

				const auto deadline = std::chrono::steady_clock::now() + timeout;

				while (true) {
					ServerTestAccess::ProcessNetworkEvents(m_Server);

					if (auto frame = reader.TryTake())
						return frame;

					if (reader.HasError()) {
						ADD_FAILURE() << "Loopback frame decoder failed";
						return std::nullopt;
					}

					auto received = client.ReceiveSome(ReceiveChunkSize, 0ms);

					if (received.has_value()) {
						if (received->empty()) {
							ADD_FAILURE()
								<< "Connection closed while waiting for a packet frame";
							return std::nullopt;
						}

						reader.Append(*received);
					}
					else if (!client.IsLastErrorTransient()) {
						ADD_FAILURE()
							<< "Socket receive failed with "
							<< client.GetLastError();
						return std::nullopt;
					}

					if (std::chrono::steady_clock::now() >= deadline)
						return std::nullopt;

					std::this_thread::sleep_for(PollInterval);
				}
			}

			[[nodiscard]]
			bool ConnectClient(
				LoopbackClient& client,
				std::size_t expected_connection_count) {

				if (!client.IsReady())
					return false;

				if (!client.Connect(m_Endpoint.Address, m_Endpoint.Port))
					return false;

				return WaitForProtocolConnectionCount(expected_connection_count);
			}

			Server m_Server;
			NetworkManager* m_Manager{ nullptr };
			NetworkEndpoint m_Endpoint;
		};

		TEST_F(
			ServerLoopbackTests,
			CompletesHandshakeStatusPingAndPong) {

			constexpr std::int64_t PingValue{ 123456789012345LL };

			LoopbackClient client;
			ASSERT_TRUE(ConnectClient(client, 1)) << client.GetLastError();

			const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
			const auto status_request = MakeStatusRequestFrame();
			const auto ping_request = MakePingRequestFrame(PingValue);

			ASSERT_TRUE(client.SendAll(handshake.Bytes()));
			PumpFor(10ms);
			ASSERT_TRUE(client.SendAll(status_request.Bytes()));

			LoopbackFrameReader reader;
			const auto status_response = ReceiveFrame(client, reader);
			ASSERT_TRUE(status_response.has_value());
			ExpectDefaultStatusResponse(*status_response);

			ASSERT_TRUE(client.SendAll(ping_request.Bytes()));
			const auto pong = ReceiveFrame(client, reader);
			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof()) << client.GetLastError();
			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			HandlesEntireStatusSequenceFragmentedOneByteAtATime) {

			constexpr std::int64_t PingValue{ 0x102030405060708LL };

			LoopbackClient client;
			ASSERT_TRUE(ConnectClient(client, 1));

			const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
			const auto status_request = MakeStatusRequestFrame();
			const auto ping_request = MakePingRequestFrame(PingValue);
			const auto request = Combine({
				handshake.Bytes(),
				status_request.Bytes(),
				ping_request.Bytes(),
			});

			for (std::size_t index{ 0 }; index < request.size(); ++index) {
				const std::span<const std::byte> single_byte(
					request.data() + index,
					1);

				ASSERT_TRUE(client.SendAll(single_byte));
				ServerTestAccess::ProcessNetworkEvents(m_Server);
				std::this_thread::sleep_for(PollInterval);
			}

			LoopbackFrameReader reader;
			const auto status_response = ReceiveFrame(client, reader);
			ASSERT_TRUE(status_response.has_value());
			ExpectDefaultStatusResponse(*status_response);

			const auto pong = ReceiveFrame(client, reader);
			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());
			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}


		TEST_F(
			ServerLoopbackTests,
			HandlesCoalescedHandshakeAndStatusRequest) {

			constexpr std::int64_t PingValue{ 55 };

			LoopbackClient client;
			ASSERT_TRUE(ConnectClient(client, 1));

			const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
			const auto status_request = MakeStatusRequestFrame();
			const auto first_write = Combine({
				handshake.Bytes(),
				status_request.Bytes(),
			});

			ASSERT_TRUE(client.SendAll(first_write));

			LoopbackFrameReader reader;
			const auto status_response = ReceiveFrame(client, reader);
			ASSERT_TRUE(status_response.has_value());
			ExpectDefaultStatusResponse(*status_response);

			const auto ping_request = MakePingRequestFrame(PingValue);
			ASSERT_TRUE(client.SendAll(ping_request.Bytes()));

			const auto pong = ReceiveFrame(client, reader);
			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());
			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			HandlesHandshakeStatusRequestAndPingInSingleSend) {

			constexpr std::int64_t PingValue{ 987654321 };

			LoopbackClient client;
			ASSERT_TRUE(ConnectClient(client, 1));

			const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
			const auto status_request = MakeStatusRequestFrame();
			const auto ping_request = MakePingRequestFrame(PingValue);
			const auto request = Combine({
				handshake.Bytes(),
				status_request.Bytes(),
				ping_request.Bytes(),
			});

			ASSERT_TRUE(client.SendAll(request));

			LoopbackFrameReader reader;
			const auto status_response = ReceiveFrame(client, reader);
			ASSERT_TRUE(status_response.has_value());
			ExpectDefaultStatusResponse(*status_response);

			const auto pong = ReceiveFrame(client, reader);
			ASSERT_TRUE(pong.has_value());
			ExpectPong(*pong, PingValue);

			EXPECT_TRUE(client.WaitForEof());
			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			SupportsMultipleSequentialStatusClients) {

			constexpr std::size_t ClientCount{ 8 };

			for (std::size_t index{ 0 }; index < ClientCount; ++index) {
				SCOPED_TRACE(::testing::Message() << "Sequential client " << index);

				LoopbackClient client;
				ASSERT_TRUE(ConnectClient(client, 1));

				const auto ping_value =
					static_cast<std::int64_t>(1000 + index);

				const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
				const auto status_request = MakeStatusRequestFrame();
				const auto ping_request = MakePingRequestFrame(ping_value);
				const auto request = Combine({
					handshake.Bytes(),
					status_request.Bytes(),
					ping_request.Bytes(),
				});

				ASSERT_TRUE(client.SendAll(request));

				LoopbackFrameReader reader;
				const auto status_response = ReceiveFrame(client, reader);
				ASSERT_TRUE(status_response.has_value());
				ExpectDefaultStatusResponse(*status_response);

				const auto pong = ReceiveFrame(client, reader);
				ASSERT_TRUE(pong.has_value());
				ExpectPong(*pong, ping_value);

				ASSERT_TRUE(client.WaitForEof());
				ASSERT_TRUE(WaitForProtocolConnectionCount(0));
			}
		}


		TEST_F(
			ServerLoopbackTests,
			SupportsMultipleConcurrentStatusClients) {

			constexpr std::size_t ClientCount{ 4 };

			std::vector<std::unique_ptr<LoopbackClient>> clients;
			clients.reserve(ClientCount);

			for (std::size_t index{ 0 }; index < ClientCount; ++index) {
				auto client = std::make_unique<LoopbackClient>();
				ASSERT_TRUE(client->IsReady());
				ASSERT_TRUE(client->Connect(m_Endpoint.Address, m_Endpoint.Port));
				clients.push_back(std::move(client));
			}

			ASSERT_TRUE(WaitForProtocolConnectionCount(ClientCount));
			std::vector<LoopbackFrameReader> readers(ClientCount);

			for (std::size_t index{ 0 }; index < ClientCount; ++index) {
				const auto ping_value =
					static_cast<std::int64_t>(5000 + index);

				const auto handshake = MakeStatusHandshakeFrame(m_Endpoint.Port);
				const auto status_request = MakeStatusRequestFrame();
				const auto ping_request = MakePingRequestFrame(ping_value);
				const auto request = Combine({
					handshake.Bytes(),
					status_request.Bytes(),
					ping_request.Bytes(),
				});

				ASSERT_TRUE(clients[index]->SendAll(request));
			}

			for (std::size_t index{ 0 }; index < ClientCount; ++index) {
				SCOPED_TRACE(::testing::Message() << "Concurrent client " << index);
				const auto ping_value =
					static_cast<std::int64_t>(5000 + index);

				const auto status_response =
					ReceiveFrame(*clients[index], readers[index]);
				ASSERT_TRUE(status_response.has_value());
				ExpectDefaultStatusResponse(*status_response);

				const auto pong =
					ReceiveFrame(*clients[index], readers[index]);
				ASSERT_TRUE(pong.has_value());
				ExpectPong(*pong, ping_value);
			}

			for (auto& client : clients)
				ASSERT_TRUE(client->WaitForEof());

			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}

		TEST_F(
			ServerLoopbackTests,
			CompletesLoginAndConfigurationThroughCoreDispatcher) {

			constexpr std::string_view Username{ "PlayerOne" };

			LoopbackClient client;
			ASSERT_TRUE(ConnectClient(client, 1)) << client.GetLastError();

			const auto snapshots = ServerTestAccess::GetClientSnapshots(m_Server);
			ASSERT_EQ(snapshots.size(), 1u);
			const ConnectionId connection = snapshots.front().Connection;

			LoopbackFrameReader reader;

			const auto handshake = MakeLoginHandshakeFrame(m_Endpoint.Port);
			ASSERT_TRUE(client.SendAll(handshake.Bytes()));
			PumpFor(10ms);

			const auto login_start = MakeLoginStartFrame(Username);
			ASSERT_TRUE(client.SendAll(login_start.Bytes()));

			const auto login_success = ReceiveFrame(client, reader);
			ASSERT_TRUE(login_success.has_value());
			ExpectLoginSuccess(*login_success, Username);

			const auto acknowledged = MakeLoginAcknowledgedFrame();
			const auto client_information = MakeClientInformationFrame();
			const auto enter_configuration = Combine({
				acknowledged.Bytes(),
				client_information.Bytes(),
			});

			ASSERT_TRUE(client.SendAll(enter_configuration));

			const auto feature_flags = ReceiveFrame(client, reader);
			ASSERT_TRUE(feature_flags.has_value());
			EXPECT_EQ(
				feature_flags->PacketId,
				Configuration::Clientbound::FeatureFlagsPacketId);

			const auto known_pack_offer = ReceiveFrame(client, reader);
			ASSERT_TRUE(known_pack_offer.has_value());
			EXPECT_EQ(
				known_pack_offer->PacketId,
				Configuration::Clientbound::SelectKnownPacksPacketId);

			const auto known_pack_selection = MakeEmptyKnownPackSelectionFrame();
			ASSERT_TRUE(client.SendAll(known_pack_selection.Bytes()));

			for (int registry{ 0 }; registry < 2; ++registry) {
				const auto registry_data = ReceiveFrame(client, reader);
				ASSERT_TRUE(registry_data.has_value());
				EXPECT_EQ(
					registry_data->PacketId,
					Configuration::Clientbound::RegistryDataPacketId);
			}

			const auto tags = ReceiveFrame(client, reader);
			ASSERT_TRUE(tags.has_value());
			EXPECT_EQ(tags->PacketId, Configuration::Clientbound::TagsPacketId);

			const auto finish_request = ReceiveFrame(client, reader);
			ASSERT_TRUE(finish_request.has_value());
			EXPECT_EQ(
				finish_request->PacketId,
				Configuration::Clientbound::FinishConfigurationPacketId);

			const auto finish_acknowledgement = MakeFinishConfigurationFrame();
			ASSERT_TRUE(client.SendAll(finish_acknowledgement.Bytes()));

			ASSERT_TRUE(PumpUntil([this, connection] {
				const auto* server_client =
					ServerTestAccess::FindClient(m_Server, connection);

				return server_client != nullptr
					&& server_client->GetProtocolState() == ProtocolState::Play;
			}));

			auto* server_client = ServerTestAccess::FindClient(m_Server, connection);
			ASSERT_NE(server_client, nullptr);
			EXPECT_EQ(server_client->GetProtocolState(), ProtocolState::Play);
			EXPECT_EQ(server_client->GetLifecycleState(), ClientLifecycleState::Play);
			ASSERT_TRUE(server_client->GetIdentity().has_value());
			EXPECT_EQ(server_client->GetIdentity()->Username, Username);

			const auto& session =
				server_client->GetProtocolConnection().GetSession();
			ASSERT_TRUE(session.GetRegistrySnapshot());
			EXPECT_EQ(
				session.GetConfigurationGeneration(),
				InitialSyntheticRegistryGeneration);
			EXPECT_EQ(
				session.GetRegistrySnapshot()->GetGeneration(),
				InitialSyntheticRegistryGeneration);

			client.Close();
			EXPECT_TRUE(WaitForProtocolConnectionCount(0));
		}
	}
}
