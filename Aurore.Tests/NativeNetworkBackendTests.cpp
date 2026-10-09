#include <gtest/gtest.h>

#include <Aurore/Network/NetworkManager.hpp>
#include <Aurore/Network/NetworkTypes.hpp>

#include <NetworkManagerTestAccess.hpp>

namespace {
	using namespace Aurore::Network;
	using namespace Aurore::Network::Detail;

	[[nodiscard]]
	NetworkConfiguration MakeNativeBackendConfig() {
		return NetworkConfiguration{
			.Backend = NetworkBackendType::Automatic,
			.BindAddress = "127.0.0.1",
			.Port = 0,
			.MaximumConnections = 16,
			.ReceiveBufferSize = 4096,
			.MaximumInboundBytesPerConnection = 64 * 1024,
			.MaximumOutboundBytesPerConnection = 64 * 1024,
			.MaximumCommandQueueEntries = 256,
			.MaximumCommandQueueBytes = 1024 * 1024,
			.MaximumEventQueueEntries = 256,
			.MaximumEventQueueBytes = 1024 * 1024,
			.MaximumTotalOutboundBytes = 4 * 1024 * 1024,
			.MaximumTotalInboundEventBytes = 4 * 1024 * 1024,
		};
	}

	void ExpectNoResources(
		const NetworkResourceSnapshot& snapshot) {

		EXPECT_EQ(snapshot.TrackedConnections, 0);
		EXPECT_EQ(snapshot.ActiveConnections, 0);
		EXPECT_EQ(snapshot.TotalOutboundBytes, 0);
		EXPECT_EQ(snapshot.TotalInboundEventBytes, 0);
	}

	TEST(
		NativeNetworkBackendLifecycleTests,
		SupportsRepeatedStartStopCycle) {

		NetworkManager manager;

		ASSERT_TRUE(
			manager.Initialize(
				MakeNativeBackendConfig())
			.has_value());

		EXPECT_TRUE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());

		for (int cycle{ 0 }; cycle < 15; cycle++) {
			SCOPED_TRACE(
				::testing::Message()
					<< "Lifecycle cycle: "
					<< cycle);

			const auto start = manager.Start();

			ASSERT_TRUE(start.has_value());
			EXPECT_NE(start->Port, 0);
			EXPECT_TRUE(manager.IsRunning());

			const auto bound =
				manager.GetBoundEndpoint();

			ASSERT_TRUE(bound.has_value());
			EXPECT_EQ(*bound, *start);

			manager.Stop();

			EXPECT_TRUE(manager.IsInitialized());
			EXPECT_FALSE(manager.IsRunning());

			EXPECT_FALSE(
				manager.GetBoundEndpoint()
					.has_value());

			ExpectNoResources(
				NetworkManagerTestAccess::
					GetResourceSnapshot(
						manager));
		}

		manager.Shutdown();

		EXPECT_FALSE(manager.IsInitialized());
		EXPECT_FALSE(manager.IsRunning());

		ExpectNoResources(
			NetworkManagerTestAccess::
				GetResourceSnapshot(
					manager));
	}

	TEST(
		NativeNetworkBackendLifecycleTests,
		ShutdownStopsRunningBackend) {

		NetworkManager manager;

		ASSERT_TRUE(
			manager.Initialize(
				MakeNativeBackendConfig())
			.has_value());

		ASSERT_TRUE(
			manager.Start().has_value());

		ASSERT_TRUE(manager.IsRunning());

		manager.Shutdown();

		EXPECT_FALSE(manager.IsRunning());
		EXPECT_FALSE(manager.IsInitialized());

		EXPECT_FALSE(
			manager.GetBoundEndpoint()
				.has_value());

		ExpectNoResources(
			NetworkManagerTestAccess::
				GetResourceSnapshot(
					manager));
	}
}
