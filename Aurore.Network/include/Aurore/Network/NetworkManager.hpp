#pragma once

#include <Aurore/Network/NetworkTypes.hpp>
#include <Aurore/Util/ByteBuffer.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace Aurore::Network::Detail {
	class NetworkBackend;
	class NetworkManagerTestAccess;
}

namespace Aurore::Network {
	/*
		Lifecycle operations must be serialized by the owning server thread.

		The network worker never calls NetworkManager directly; it communicates
		only through the internal command and event queues.
	*/
	class NetworkManager final {
	public:
		NetworkManager();
		~NetworkManager() noexcept;

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		NetworkManager(NetworkManager&&) = delete;
		NetworkManager& operator=(NetworkManager&&) = delete;

		[[nodiscard]] NetworkResult<void> Initialize(NetworkConfiguration config);
		[[nodiscard]] NetworkResult<NetworkEndpoint> Start();

		void Stop() noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] std::vector<NetworkEvent> DrainEvents();
		[[nodiscard]] NetworkResult<void> QueueSend(ConnectionId connection, Aurore::Util::ByteBuffer data);
		[[nodiscard]] NetworkResult<void> CloseAfterFlush(ConnectionId connection, ConnectionCloseReason reason = ConnectionCloseReason::ApplicationRequested);
		[[nodiscard]] NetworkResult<void> CloseImmediately(ConnectionId connection, ConnectionCloseReason reason = ConnectionCloseReason::ApplicationRequested);
		[[nodiscard]] NetworkResult<void> ResumeReceive(ConnectionId connection);

		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] bool IsRunning() const noexcept;

		[[nodiscard]] std::optional<NetworkConfiguration> GetConfiguration() const;
		[[nodiscard]] std::optional<NetworkEndpoint> GetBoundEndpoint() const;

	private:
		friend class Detail::NetworkManagerTestAccess;

		[[nodiscard]] bool InstallBackendForTesting(std::unique_ptr<Detail::NetworkBackend> backend);
		[[nodiscard]] NetworkResourceSnapshot GetResourceSnapshotForTesting() const noexcept;
		[[nodiscard]] NetworkQueueSnapshot GetCommandQueueSnapshotForTesting() const noexcept;
		[[nodiscard]] NetworkQueueSnapshot GetEventQueueSnapshotForTesting() const noexcept;

		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

