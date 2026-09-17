#include <gtest/gtest.h>

#include <Aurore/Core/ClientLifecycle.hpp>

#include <Aurore/Protocol/PacketFrame.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Aurore::Tests {
	namespace {
		using namespace std::chrono_literals;

		using Aurore::Core::ClientCloseCause;
		using Aurore::Core::ClientConfiguration;
		using Aurore::Core::ClientError;
		using Aurore::Core::ClientLifecycleState;
		using Aurore::Core::ClientReceiveDisposition;
		using Aurore::Core::ConnectionManager;

		using Aurore::Network::ConnectionId;
		using Aurore::Network::NetworkEndpoint;

		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ServerStatus;

		constexpr ConnectionId FirstConnection{ .Value = 1 };

		constexpr ConnectionId SecondConnection{ .Value = 2 };

		NetworkEndpoint MakeLocalEndpoint() {
			return NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 25565,
			};
		}

		NetworkEndpoint MakeRemoteEndpoint(std::uint16_t port = 50000) {

			return NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = port,
			};
		}

		ServerStatus MakeServerStatus() {
			return ServerStatus{
				.VersionName = "Aurore Test",
				.ProtocolVersion = 774,
				.MaximumPlayers = 20,
				.OnlinePlayers = 0,
				.Description = "Lifecycle Test",
			};
		}

		Aurore::Util::ByteBuffer MakeHandshakePacket(std::int32_t intention) {
			Aurore::Util::ByteBuffer writer;

			writer.WriteVarInt(774);
			writer.WriteString("localhost");
			writer.WriteUInt16(25565);
			writer.WriteVarInt(intention);
			auto data = writer.Bytes();

			return EncodePacketFrame(0x00, std::move(data));
		}
	}

	TEST(ClientLifecycleTests, OpenCreatesHandshakeClient) {
		ConnectionManager manager;
		const auto connected_at = ConnectionManager::Clock::now();
		auto result = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus(), connected_at);
		ASSERT_TRUE(result.has_value());

		const auto& client = result->get();
		EXPECT_EQ(client.GetConnectionId(), FirstConnection);
		EXPECT_EQ(client.GetLifecycleState(), ClientLifecycleState::Handshake);
		EXPECT_TRUE(client.IsTransportOpen());
		EXPECT_FALSE(client.IsCloseRequested());
		EXPECT_EQ(manager.Size(), 1);
	}

	TEST(ClientLifecycleTests, OpenRejectsInvalidConnection) {
		ConnectionManager manager;
		const auto result = manager.Open(ConnectionId{}, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus());

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error(), ClientError::InvalidConnection);
		EXPECT_TRUE(manager.Empty());
	}

	TEST(ClientLifecycleTests, OpenRejectsDuplicateConnection) {
		ConnectionManager manager;
		ASSERT_TRUE(manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus()).has_value());

		const auto duplicate = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(50001), MakeServerStatus());

		ASSERT_FALSE(duplicate.has_value());
		EXPECT_EQ(duplicate.error(), ClientError::DuplicateConnection);
		EXPECT_EQ(manager.Size(), 1);
	}

	TEST(ClientLifecycleTests, OpenEnforcesCapacity) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 1,
			}
		};

		ASSERT_TRUE(manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus()).has_value());

		const auto second = manager.Open(SecondConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(50001), MakeServerStatus());

		ASSERT_FALSE(second.has_value());
		EXPECT_EQ(second.error(), ClientError::CapacityExceeded);
	}

	TEST(ClientLifecycleTests, HandshakeUpdatesLifecycleState) {
		ConnectionManager manager;

		auto open_result = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus());
		ASSERT_TRUE(open_result.has_value());

		auto& client = open_result->get();
		const auto handshake = MakeHandshakePacket(1);
		const auto bytes = std::span<const std::byte>{ handshake.Data(), handshake.Size() };
		const auto receive_result = client.Receive(bytes);
		EXPECT_EQ(receive_result.Disposition, ClientReceiveDisposition::KeepOpen);
		EXPECT_FALSE(receive_result.HasError());
		EXPECT_EQ(client.GetLifecycleState(), ClientLifecycleState::Status);
	}

	TEST(ClientLifecycleTests, ProtocolFailureMarksClientClosing) {
		ConnectionManager manager;

		auto open_result = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus());
		ASSERT_TRUE(open_result.has_value());

		auto& client = open_result->get();

		const std::vector<std::byte> malformed{
			std::byte{ 0xFF },
			std::byte{ 0xFF },
			std::byte{ 0xFF },
			std::byte{ 0xFF },
			std::byte{ 0xFF },
		};

		const auto result = client.Receive(malformed);
		EXPECT_TRUE(result.HasError());
		EXPECT_EQ(result.Disposition, ClientReceiveDisposition::CloseImmediately);
		EXPECT_TRUE(client.IsCloseRequested());
		EXPECT_EQ(client.GetCloseCause(), ClientCloseCause::ProtocolFailure);
		EXPECT_EQ(client.GetLifecycleState(), ClientLifecycleState::Closing);
	}

	TEST(ClientLifecycleTests, HandshakeTimeoutIsReported) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 16,
				.HandshakeTimeout = 5s,
				.LoginTimeout = 30s,
				.ConfigurationTimeout = 30s,
				.IdleTimeout = 5min,
			}
		};

		const auto connected_at = ConnectionManager::Clock::now();
		ASSERT_TRUE(manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus(), connected_at).has_value());

		const auto timeouts = manager.CollectTimeouts(connected_at + 6s);
		ASSERT_EQ(timeouts.size(), 1);
		EXPECT_EQ(timeouts.front().Connection, FirstConnection);
		EXPECT_EQ(timeouts.front().Cause, ClientCloseCause::HandshakeTimeout);
	}

	TEST(ClientLifecycleTests, ClosingClientDoesNotProduceTimeout) {
		ConnectionManager manager{
			ClientConfiguration{
				.MaximumClients = 16,
				.HandshakeTimeout = 5s,
				.LoginTimeout = 30s,
				.ConfigurationTimeout = 30s,
				.IdleTimeout = 5min,
			}
		};

		const auto connected_at = ConnectionManager::Clock::now();

		auto open_result = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus(), connected_at);
		ASSERT_TRUE(open_result.has_value());

		open_result->get().MarkCloseRequested(ClientCloseCause::ServerStopping);
		EXPECT_TRUE(manager.CollectTimeouts(connected_at + 1min).empty());
	}

	TEST(ClientLifecycleTests, TransportClosurePreservesSnapshotUntilRemoval) {
		ConnectionManager manager;
		auto open_result = manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus());
		ASSERT_TRUE(open_result.has_value());
		ASSERT_TRUE(manager.MarkTransportClosed(FirstConnection, ClientCloseCause::TransportFailure));

		const auto* client = manager.Find(FirstConnection);
		ASSERT_NE(client, nullptr);
		EXPECT_FALSE(client->IsTransportOpen());
		EXPECT_TRUE(client->IsClosed());
		EXPECT_EQ(client->GetLifecycleState(), ClientLifecycleState::Closed);
		EXPECT_EQ(client->GetCloseCause(), ClientCloseCause::TransportFailure);
		EXPECT_TRUE(manager.Remove(FirstConnection));
		EXPECT_TRUE(manager.Empty());
	}

	TEST(ClientLifecycleTests, ConfigurationCannotShrinkBelowCurrentUsage) {
		ConnectionManager manager;
		ASSERT_TRUE(manager.Open(FirstConnection, MakeLocalEndpoint(), MakeRemoteEndpoint(), MakeServerStatus()).has_value());

		ClientConfiguration config = manager.GetConfiguration();

		config.MaximumClients = 0;
		EXPECT_FALSE(manager.SetConfiguration(config));

		config.MaximumClients = 1;
		EXPECT_TRUE(manager.SetConfiguration(config));
	}
}
