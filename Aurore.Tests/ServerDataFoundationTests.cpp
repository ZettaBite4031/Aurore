#include <Aurore/Core/Server.hpp>
#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <../src/ServerTestAccess.hpp>

#include <gtest/gtest.h>

namespace Aurore::Tests {
	using Aurore::Core::InitialSyntheticRegistryGeneration;
	using Aurore::Core::Server;
	using Aurore::Core::Detail::ServerTestAccess;

	TEST(ServerDataFoundationTests, InitializesCanonicalSnapshotIdempotently) {
		Server server;

		ASSERT_TRUE(ServerTestAccess::InitializeDataFoundations(server));
		const auto& store = ServerTestAccess::GetRegistrySnapshotStore(server);
		const auto first_snapshot = store.GetActiveSnapshot();

		ASSERT_NE(first_snapshot, nullptr);
		EXPECT_EQ(first_snapshot->GetGeneration(), InitialSyntheticRegistryGeneration);
		EXPECT_EQ(first_snapshot->GetBlockTags().GetGeneration(), InitialSyntheticRegistryGeneration);
		EXPECT_EQ(first_snapshot->GetItemTags().GetGeneration(), InitialSyntheticRegistryGeneration);

		ASSERT_TRUE(ServerTestAccess::InitializeDataFoundations(server));
		EXPECT_EQ(store.GetActiveSnapshot().get(), first_snapshot.get());
	}
}

