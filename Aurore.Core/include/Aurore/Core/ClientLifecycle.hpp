#pragma once

#include <Aurore/Util/UUID.hpp>

#include <Aurore/Network/NetworkTypes.hpp>

#include <Aurore/Protocol/ProtocolConnection.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Aurore::Core {
	enum class ClientLifecycleState : std::uint8_t {
		Handshake,
		Status,
		Login,
		Configuration,
		Play,
		Closing,
		Closed,
	};

	enum class ClientCloseCause : std::uint8_t {
		None,
		ProtocolRequested,
		ProtocolFailure,
		TransportClosed,
		TransportFailure,
		ServerStopping,
		HandshakeTimeout,
		LoginTimeout,
		ConfigurationTimeout,
		IdleTimeout,
		AdmissionRejected,
	};

	enum class ClientError : std::uint8_t {
		InvalidConnection,
		DuplicateConnection,
		ConnectionNotFound,
		ConnectionClosed,
		InvalidState,
		CapacityExceeded,

		InvalidIdentity,
		IdentityAlreadyAssigned,
		DuplicateUsername,
		DuplicateUniqueId,
		PlayerCapacityExceeded,
	};

	enum class ClientReceiveDisposition : std::uint8_t {
		KeepOpen,
		CloseAfterFlush,
		CloseImmediately,
	};

	struct ClientConfiguration final {
		std::size_t MaximumClients{ 1024 };
		std::size_t MaximumPlayers{ 20 };

		std::chrono::milliseconds HandshakeTimeout{ std::chrono::seconds(10) };
		std::chrono::milliseconds LoginTimeout{ std::chrono::seconds{ 30 } };
		std::chrono::milliseconds ConfigurationTimeout{ std::chrono::seconds{ 30 } };
		std::chrono::milliseconds IdleTimeout{ std::chrono::seconds{ 30 } };

		/*
			Every ClientConnection receives the same validated protocol limits.
			The server binds MaximumOutboundActionBytes to the configured network
			retention budget so an action is never built when transport cannot
			retain it.
		*/
		Protocol::ProtocolPipelineConfiguration ProtocolPipeline{};
	};

	struct ClientIdentity final {
		std::string Username;
		Aurore::Util::Uuid UniqueId;

		std::vector<Protocol::ProfileProperty> Properties;

		[[nodiscard]] bool IsComplete() const noexcept {
			return !Username.empty() && !UniqueId.IsNil();
		}
	};

	struct ClientReceiveResult final {
		std::vector<Aurore::Util::ByteBuffer> OutboundFrames;
		std::vector<Protocol::ProtocolRequest> Requests;

		ClientReceiveDisposition Disposition{ ClientReceiveDisposition::KeepOpen };

		std::optional<Protocol::ProtocolConnectionError> ProtocolError;
		std::optional<Protocol::ProtocolFailureContext> FailureContext;

		[[nodiscard]] bool HasError() const noexcept {
			return ProtocolError.has_value();
		}
	};

	struct ClientTimeout final {
		Network::ConnectionId Connection;
		ClientCloseCause Cause{ ClientCloseCause::None };
	};

	struct ClientSnapshot final {
		Network::ConnectionId Connection;

		Network::NetworkEndpoint LocalEndpoint;
		Network::NetworkEndpoint RemoteEndpoint;

		ClientLifecycleState State{ ClientLifecycleState::Handshake };

		ClientCloseCause CloseCause{ ClientCloseCause::None };

		std::optional<ClientIdentity> Identity;

		std::chrono::steady_clock::time_point ConnectedAt;
		std::chrono::steady_clock::time_point LastReceiveAt;
		std::chrono::steady_clock::time_point LastActivityAt;
		std::chrono::steady_clock::time_point StateEnteredAt;

		std::size_t BufferedProtocolBytes{ 0 };

		bool TransportOpen{ true };
		bool CloseRequested{ false };
	};

	class ClientConnection final {
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		/*
			Compatibility overload retained for direct construction in existing
			call sites and tests.
		*/
		ClientConnection(Network::ConnectionId connection,
			Network::NetworkEndpoint local_endpoint, Network::NetworkEndpoint remote_endpoint,
			Protocol::ServerStatus server_status, TimePoint connected_at = Clock::now());

		ClientConnection(Network::ConnectionId connection,
			Network::NetworkEndpoint local_endpoint, Network::NetworkEndpoint remote_endpoint,
			Protocol::ServerStatus server_status,
			Protocol::ProtocolPipelineConfiguration pipeline_configuration,
			TimePoint connected_at = Clock::now());

		ClientConnection(const ClientConnection&) = delete;
		ClientConnection& operator=(const ClientConnection&) = delete;

		ClientConnection(ClientConnection&&) = default;
		ClientConnection& operator=(ClientConnection&&) = default;

		[[nodiscard]] ClientReceiveResult Receive(std::span<const std::byte> bytes, TimePoint received_at = Clock::now());
		[[nodiscard]] ClientReceiveResult ResolveLogin(const Protocol::LoginResolution& resolution, TimePoint resolved_at = Clock::now());
		[[nodiscard]] ClientReceiveResult ResolveConfiguration(Protocol::ConfigurationResolution resolution, TimePoint resolved_at = Clock::now());

		void MarkCloseRequested(ClientCloseCause cause = ClientCloseCause::ProtocolRequested) noexcept;
		void MarkTransportClosed(ClientCloseCause cause = ClientCloseCause::TransportClosed) noexcept;

		[[nodiscard]] std::optional<ClientTimeout> CheckTimeout(const ClientConfiguration& config, TimePoint now = Clock::now()) const noexcept;

		[[nodiscard]] ClientSnapshot GetSnapshot() const;

		[[nodiscard]] Network::ConnectionId GetConnectionId() const noexcept;

		[[nodiscard]] const Network::NetworkEndpoint& GetLocalEndpoint() const noexcept;
		[[nodiscard]] const Network::NetworkEndpoint& GetRemoteEndpoint() const noexcept;

		[[nodiscard]] const Protocol::ProtocolConnection& GetProtocolConnection() const noexcept;
		[[nodiscard]] Protocol::ProtocolConnection& GetProtocolConnection() noexcept;

		[[nodiscard]] Protocol::ProtocolState GetProtocolState() const noexcept;

		[[nodiscard]] ClientLifecycleState GetLifecycleState() const noexcept;

		[[nodiscard]] ClientCloseCause GetCloseCause() const noexcept;

		[[nodiscard]] const std::optional<ClientIdentity>& GetIdentity() const noexcept;

		[[nodiscard]] TimePoint GetConnectedAt() const noexcept;
		[[nodiscard]] TimePoint GetLastReceiveAt() const noexcept;
		[[nodiscard]] TimePoint GetLastActivityAt() const noexcept;
		[[nodiscard]] TimePoint GetStateEnteredAt() const noexcept;

		[[nodiscard]] bool IsTransportOpen() const noexcept;
		[[nodiscard]] bool IsCloseRequested() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;

	private:
		friend class ConnectionManager;

		[[nodiscard]] ClientReceiveResult ApplyProtocolResult(Protocol::ProtocolProcessResult result, TimePoint observed_at);

		[[nodiscard]] ClientLifecycleState DetermineLifecycleState() const noexcept;

		[[nodiscard]] ClientReceiveDisposition TranslateDisposition(Protocol::ConnectionDisposition disposition) const noexcept;

		void AssignIdentity(ClientIdentity identity) noexcept;
		void ClearIdentity() noexcept;

		void RefreshLifecycleState(TimePoint observed_at = Clock::now()) noexcept;

		Network::ConnectionId m_Connection;

		Network::NetworkEndpoint m_LocalEndpoint;
		Network::NetworkEndpoint m_RemoteEndpoint;

		Protocol::ProtocolConnection m_ProtocolConnection;

		std::optional<ClientIdentity> m_Identity;

		TimePoint m_ConnectedAt;
		TimePoint m_LastReceiveAt;
		TimePoint m_LastActivityAt;
		TimePoint m_StateEnteredAt;

		ClientLifecycleState m_LifecycleState{ ClientLifecycleState::Handshake };

		ClientCloseCause m_CloseCause{ ClientCloseCause::None };

		bool m_TransportOpen{ true };
		bool m_CloseRequested{ false };
	};

	class ConnectionManager final {
	public:
		using Clock = ClientConnection::Clock;
		using TimePoint = ClientConnection::TimePoint;

		explicit ConnectionManager(ClientConfiguration config = {});

		ConnectionManager(const ConnectionManager&) = delete;
		ConnectionManager& operator=(const ConnectionManager&) = delete;

		ConnectionManager(ConnectionManager&&) = delete;
		ConnectionManager& operator=(ConnectionManager&&) = delete;

		[[nodiscard]] std::expected<std::reference_wrapper<ClientConnection>, ClientError> Open(Network::ConnectionId connection,
			Network::NetworkEndpoint local_endpoint, Network::NetworkEndpoint remote_endpoint, Protocol::ServerStatus, TimePoint connected_at = Clock::now());

		[[nodiscard]] std::expected<std::reference_wrapper<ClientConnection>, ClientError> AdmitIdentity(Network::ConnectionId connection, ClientIdentity identity);

		[[nodiscard]] bool ReleaseIdentity(Network::ConnectionId connection) noexcept;

		[[nodiscard]] ClientConnection* Find(Network::ConnectionId connection) noexcept;
		[[nodiscard]] const ClientConnection* Find(Network::ConnectionId connection) const noexcept;

		[[nodiscard]] ClientConnection* FindByUsername(std::string_view username) noexcept;
		[[nodiscard]] const ClientConnection* FindByUsername(std::string_view username) const noexcept;

		[[nodiscard]] ClientConnection* FindByUniqueId(const Aurore::Util::Uuid& unique_id) noexcept;
		[[nodiscard]] const ClientConnection* FindByUniqueId(const Aurore::Util::Uuid& unique_id) const noexcept;

		[[nodiscard]] bool Contains(Network::ConnectionId connection) const noexcept;

		[[nodiscard]] bool MarkTransportClosed(Network::ConnectionId connection, ClientCloseCause cause = ClientCloseCause::TransportClosed) noexcept;

		[[nodiscard]] bool Remove(Network::ConnectionId connection) noexcept;

		[[nodiscard]] std::size_t AuthenticatedCount() const noexcept;

		[[nodiscard]] std::vector<ClientTimeout> CollectTimeouts(TimePoint now = Clock::now()) const;

		[[nodiscard]] std::vector<ClientSnapshot> GetSnapshots() const;

		void MarkAllClosing(ClientCloseCause cause = ClientCloseCause::ServerStopping) noexcept;

		void Clear() noexcept;

		[[nodiscard]] std::size_t Size() const noexcept;

		[[nodiscard]] bool Empty() const noexcept;

		[[nodiscard]] const ClientConfiguration& GetConfiguration() const noexcept;

		[[nodiscard]] bool SetConfiguration(ClientConfiguration config) noexcept;

	private:
		[[nodiscard]] static bool ValidateConfiguration(const ClientConfiguration& config) noexcept;

		[[nodiscard]] static std::string NormalizeUsername(std::string_view username);

		ClientConfiguration m_Config;

		std::unordered_map<Network::ConnectionId, ClientConnection> m_Connections;
		std::unordered_map<std::string, Network::ConnectionId> m_UsernameIndex;
		std::unordered_map<Aurore::Util::Uuid, Network::ConnectionId> m_UniqueIdIndex;
	};
}

