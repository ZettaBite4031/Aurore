#include <Aurore/Core/ClientLifecycle.hpp>

#include <algorithm>
#include <utility>

namespace Aurore::Core {
	ClientConnection::ClientConnection(Network::ConnectionId connection, Network::NetworkEndpoint local_endpoint,
		Network::NetworkEndpoint remote_endpoint, Protocol::ServerStatus server_status, TimePoint connected_at)
		: ClientConnection(connection, std::move(local_endpoint), std::move(remote_endpoint),
			std::move(server_status), Protocol::ProtocolPipelineConfiguration{}, connected_at) {}

	ClientConnection::ClientConnection(Network::ConnectionId connection, Network::NetworkEndpoint local_endpoint,
		Network::NetworkEndpoint remote_endpoint, Protocol::ServerStatus server_status,
		Protocol::ProtocolPipelineConfiguration pipeline_configuration, TimePoint connected_at)
		: m_Connection(connection), m_LocalEndpoint(std::move(local_endpoint)), m_RemoteEndpoint(std::move(remote_endpoint)),
		m_ProtocolConnection(std::move(server_status), pipeline_configuration), m_ConnectedAt(connected_at),
		m_LastReceiveAt(connected_at), m_LastActivityAt(connected_at), m_StateEnteredAt(connected_at) {
		RefreshLifecycleState(connected_at);
	}

	ClientReceiveResult ClientConnection::Receive(std::span<const std::byte> bytes, TimePoint received_at) {
		if (!m_TransportOpen || m_CloseRequested)
			return ClientReceiveResult{ .Disposition = ClientReceiveDisposition::CloseImmediately };
		m_LastReceiveAt = received_at;
		m_LastActivityAt = received_at;
		return ApplyProtocolResult(m_ProtocolConnection.Receive(bytes), received_at);
	}

	ClientReceiveResult ClientConnection::ResolveLogin(const Protocol::LoginResolution& resolution, TimePoint resolved_at) {
		if (!m_TransportOpen || m_CloseRequested)
			return ClientReceiveResult{ .Disposition = ClientReceiveDisposition::CloseImmediately };
		m_LastActivityAt = resolved_at;
		return ApplyProtocolResult(m_ProtocolConnection.ResolveLogin(resolution), resolved_at);
	}

	ClientReceiveResult ClientConnection::ResolveConfiguration(Protocol::ConfigurationResolution resolution, TimePoint resolved_at) {
		if (!m_TransportOpen || m_CloseRequested)
			return ClientReceiveResult{ .Disposition = ClientReceiveDisposition::CloseImmediately };

		m_LastActivityAt = resolved_at;

		return ApplyProtocolResult(m_ProtocolConnection.ResolveConfiguration(std::move(resolution)), resolved_at);
	}

	void ClientConnection::MarkCloseRequested(ClientCloseCause cause) noexcept {
		m_CloseRequested = true;
		if (m_CloseCause == ClientCloseCause::None)
			m_CloseCause = cause;
		RefreshLifecycleState();
	}

	void ClientConnection::MarkTransportClosed(ClientCloseCause cause) noexcept {
		m_TransportOpen = false;
		m_CloseRequested = true;
		if (m_CloseCause == ClientCloseCause::None || cause == ClientCloseCause::TransportFailure || cause == ClientCloseCause::ServerStopping)
			m_CloseCause = cause;
		RefreshLifecycleState();
	}

	void ClientConnection::AssignIdentity(ClientIdentity identity) noexcept {
		m_Identity = std::move(identity);
	}

	void ClientConnection::ClearIdentity() noexcept {
		m_Identity.reset();
	}

	std::optional<ClientTimeout> ClientConnection::CheckTimeout(const ClientConfiguration& config, TimePoint now) const noexcept {
		if (!m_TransportOpen || m_CloseRequested) return std::nullopt;

		const auto state_age = now - m_StateEnteredAt;

		switch (m_LifecycleState) {
		case ClientLifecycleState::Handshake:
			if (state_age >= config.HandshakeTimeout) {
				return ClientTimeout{
					.Connection = m_Connection,
					.Cause = ClientCloseCause::HandshakeTimeout,
				};
			}
			break;
		case ClientLifecycleState::Login:
			if (state_age >= config.LoginTimeout) {
				return ClientTimeout{
					.Connection = m_Connection,
					.Cause = ClientCloseCause::LoginTimeout,
				};
			}
			break;
		case ClientLifecycleState::Configuration:
			if (state_age >= config.ConfigurationTimeout) {
				return ClientTimeout{
					.Connection = m_Connection,
					.Cause = ClientCloseCause::ConfigurationTimeout,
				};
			}
			break;
		case ClientLifecycleState::Status:
		case ClientLifecycleState::Play:
			if (now - m_LastReceiveAt >= config.IdleTimeout) {
				return ClientTimeout{
					.Connection = m_Connection,
					.Cause = ClientCloseCause::IdleTimeout,
				};
			}
			break;
		case ClientLifecycleState::Closing:
		case ClientLifecycleState::Closed:
			break;
		}

		return std::nullopt;
	}

	ClientSnapshot ClientConnection::GetSnapshot() const {
		return ClientSnapshot{
			.Connection = m_Connection,
			.LocalEndpoint = m_LocalEndpoint,
			.RemoteEndpoint = m_RemoteEndpoint,
			.State = m_LifecycleState,
			.CloseCause = m_CloseCause,
			.Identity = m_Identity,
			.ConnectedAt = m_ConnectedAt,
			.LastReceiveAt = m_LastReceiveAt,
			.LastActivityAt = m_LastActivityAt,
			.StateEnteredAt = m_StateEnteredAt,
			.BufferedProtocolBytes = m_ProtocolConnection.BufferedBytes(),
			.TransportOpen = m_TransportOpen,
			.CloseRequested = m_CloseRequested,
		};
	}

	Network::ConnectionId ClientConnection::GetConnectionId() const noexcept {
		return m_Connection;
	}

	const Network::NetworkEndpoint& ClientConnection::GetLocalEndpoint() const noexcept {
		return m_LocalEndpoint;
	}

	const Network::NetworkEndpoint& ClientConnection::GetRemoteEndpoint() const noexcept {
		return m_RemoteEndpoint;
	}

	const Protocol::ProtocolConnection& ClientConnection::GetProtocolConnection() const noexcept {
		return m_ProtocolConnection;
	}

	Protocol::ProtocolConnection& ClientConnection::GetProtocolConnection() noexcept {
		return m_ProtocolConnection;
	}

	Protocol::ProtocolState ClientConnection::GetProtocolState() const noexcept {
		return m_ProtocolConnection.GetSession().GetState();
	}

	ClientLifecycleState ClientConnection::GetLifecycleState() const noexcept {
		return m_LifecycleState;
	}

	ClientCloseCause ClientConnection::GetCloseCause() const noexcept {
		return m_CloseCause;
	}

	const std::optional<ClientIdentity>& ClientConnection::GetIdentity() const noexcept {
		return m_Identity;
	}

	ClientConnection::TimePoint ClientConnection::GetConnectedAt() const noexcept {
		return m_ConnectedAt;
	}

	ClientConnection::TimePoint ClientConnection::GetLastReceiveAt() const noexcept {
		return m_LastReceiveAt;
	}

	ClientConnection::TimePoint ClientConnection::GetLastActivityAt() const noexcept {
		return m_LastActivityAt;
	}

	ClientConnection::TimePoint ClientConnection::GetStateEnteredAt() const noexcept {
		return m_StateEnteredAt;
	}

	bool ClientConnection::IsTransportOpen() const noexcept {
		return m_TransportOpen;
	}

	bool ClientConnection::IsCloseRequested() const noexcept {
		return m_CloseRequested;
	}

	bool ClientConnection::IsClosed() const noexcept {
		return !m_TransportOpen;
	}

	ClientReceiveResult ClientConnection::ApplyProtocolResult(Protocol::ProtocolProcessResult protocol_result, TimePoint observed_at) {
		ClientReceiveResult result;

		result.OutboundFrames = std::move(protocol_result.OutboundFrames);
		result.Requests = std::move(protocol_result.Requests);
		result.Disposition = TranslateDisposition(protocol_result.Disposition);
		result.ProtocolError = std::move(protocol_result.Error);
		result.FailureContext = std::move(protocol_result.FailureContext);

		if (result.HasError())
			m_CloseCause = ClientCloseCause::ProtocolFailure;
		else if (result.Disposition != ClientReceiveDisposition::KeepOpen)
			m_CloseCause = ClientCloseCause::ProtocolRequested;

		if (result.Disposition != ClientReceiveDisposition::KeepOpen)
			m_CloseRequested = true;

		RefreshLifecycleState(observed_at);
		return result;
	}

	ClientLifecycleState ClientConnection::DetermineLifecycleState() const noexcept {
		if (!m_TransportOpen)
			return ClientLifecycleState::Closed;

		if (m_CloseRequested || m_ProtocolConnection.IsClosing())
			return ClientLifecycleState::Closing;

		switch (m_ProtocolConnection.GetSession().GetState()) {
		case Protocol::ProtocolState::Handshake:
			return ClientLifecycleState::Handshake;
		case Protocol::ProtocolState::Status:
			return ClientLifecycleState::Status;
		case Protocol::ProtocolState::Login:
			return ClientLifecycleState::Login;
		case Protocol::ProtocolState::Configuration:
			return ClientLifecycleState::Configuration;
		case Protocol::ProtocolState::Play:
			return ClientLifecycleState::Play;
		case Protocol::ProtocolState::Disconnected:
			return ClientLifecycleState::Closing;
		}
		return ClientLifecycleState::Closing;
	}

	ClientReceiveDisposition ClientConnection::TranslateDisposition(Protocol::ConnectionDisposition disposition) const noexcept {
		switch (disposition) {
		case Protocol::ConnectionDisposition::KeepOpen:
			return ClientReceiveDisposition::KeepOpen;
		case Protocol::ConnectionDisposition::CloseAfterFlush:
			return ClientReceiveDisposition::CloseAfterFlush;
		case Protocol::ConnectionDisposition::CloseImmediately:
			return ClientReceiveDisposition::CloseImmediately;
		}
		return ClientReceiveDisposition::CloseImmediately;
	}

	void ClientConnection::RefreshLifecycleState(TimePoint observed_at) noexcept {
		const auto next_state = DetermineLifecycleState();
		if (next_state == m_LifecycleState) return;
		m_LifecycleState = next_state;
		m_StateEnteredAt = observed_at;
	}

	// CONNECTION MANAGER -----------
	ConnectionManager::ConnectionManager(ClientConfiguration config)
		: m_Config(ValidateConfiguration(config) ? config : ClientConfiguration{}) {}

	std::expected<std::reference_wrapper<ClientConnection>, ClientError> ConnectionManager::Open(Network::ConnectionId connection,
		Network::NetworkEndpoint local_endpoint, Network::NetworkEndpoint remote_endpoint, Protocol::ServerStatus server_status, TimePoint connected_at) {
		if (!connection)
			return std::unexpected(ClientError::InvalidConnection);

		if (m_Connections.contains(connection))
			return std::unexpected(ClientError::DuplicateConnection);

		if (m_Connections.size() >= m_Config.MaximumClients)
			return std::unexpected(ClientError::CapacityExceeded);

		auto [iterator, inserted] = m_Connections.try_emplace(
			connection,
			connection,
			std::move(local_endpoint),
			std::move(remote_endpoint),
			std::move(server_status),
			m_Config.ProtocolPipeline,
			connected_at);

		if (!inserted)
			return std::unexpected(ClientError::DuplicateConnection);
		return std::ref(iterator->second);
	}

	std::expected<std::reference_wrapper<ClientConnection>, ClientError> ConnectionManager::AdmitIdentity(Network::ConnectionId connection, ClientIdentity identity) {
		auto* client = Find(connection);
		if (client == nullptr)
			return std::unexpected(ClientError::ConnectionNotFound);
		if (client->IsClosed() || client->IsCloseRequested())
			return std::unexpected(ClientError::ConnectionClosed);
		if (client->GetLifecycleState() != ClientLifecycleState::Login)
			return std::unexpected(ClientError::InvalidState);
		if (client->GetIdentity())
			return std::unexpected(ClientError::IdentityAlreadyAssigned);
		if (!identity.IsComplete())
			return std::unexpected(ClientError::InvalidIdentity);
		if (AuthenticatedCount() >= m_Config.MaximumPlayers)
			return std::unexpected(ClientError::PlayerCapacityExceeded);

		const auto username_key = NormalizeUsername(identity.Username);
		if (m_UsernameIndex.contains(username_key))
			return std::unexpected(ClientError::DuplicateUsername);
		if (m_UniqueIdIndex.contains(identity.UniqueId))
			return std::unexpected(ClientError::DuplicateUniqueId);

		const auto unique_id = identity.UniqueId;

		bool username_inserted{ false };
		bool unique_id_inserted{ false };
		try {
			const auto [username_iterator, inserted_username] = m_UsernameIndex.emplace(username_key, connection);
			(void)username_iterator;

			if (!inserted_username)
				return std::unexpected(ClientError::DuplicateUsername);

			username_inserted = true;

			const auto [unique_id_iterator, inserted_unique_id] = m_UniqueIdIndex.emplace(unique_id, connection);
			(void)unique_id_iterator;

			if (!inserted_unique_id) {
				if (inserted_username) m_UsernameIndex.erase(username_key);
				return std::unexpected(ClientError::DuplicateUniqueId);
			}

			unique_id_inserted = true;

			client->AssignIdentity(std::move(identity));

			return std::ref(*client);
		}
		catch (...) {
			if (username_inserted)
				m_UsernameIndex.erase(username_key);
			if (unique_id_inserted)
				m_UniqueIdIndex.erase(unique_id);
			throw;
		}
	}

	bool ConnectionManager::ReleaseIdentity(Network::ConnectionId connection) noexcept {
		auto* client = Find(connection);
		if (client == nullptr || !client->GetIdentity()) return false;

		const auto username_key = NormalizeUsername(client->GetIdentity()->Username);
		const auto unique_id = client->GetIdentity()->UniqueId;

		m_UsernameIndex.erase(username_key);
		m_UniqueIdIndex.erase(unique_id);

		client->ClearIdentity();

		return true;
	}

	ClientConnection* ConnectionManager::Find(Network::ConnectionId connection) noexcept {
		const auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return nullptr;
		return &iterator->second;
	}

	const ClientConnection* ConnectionManager::Find(Network::ConnectionId connection) const noexcept {
		const auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return nullptr;
		return &iterator->second;
	}

	ClientConnection* ConnectionManager::FindByUsername(std::string_view username) noexcept {
		const auto iterator = m_UsernameIndex.find(NormalizeUsername(username));
		if (iterator == m_UsernameIndex.end()) return nullptr;
		return Find(iterator->second);
	}

	const ClientConnection* ConnectionManager::FindByUsername(std::string_view username) const noexcept {
		const auto iterator = m_UsernameIndex.find(NormalizeUsername(username));
		if (iterator == m_UsernameIndex.end()) return nullptr;
		return Find(iterator->second);
	}

	ClientConnection* ConnectionManager::FindByUniqueId(const Aurore::Util::Uuid& unique_id) noexcept {
		const auto iterator = m_UniqueIdIndex.find(unique_id);
		if (iterator == m_UniqueIdIndex.end()) return nullptr;
		return Find(iterator->second);
	}

	const ClientConnection* ConnectionManager::FindByUniqueId(const Aurore::Util::Uuid& unique_id) const noexcept {
		const auto iterator = m_UniqueIdIndex.find(unique_id);
		if (iterator == m_UniqueIdIndex.end()) return nullptr;
		return Find(iterator->second);
	}

	bool ConnectionManager::Contains(Network::ConnectionId connection) const noexcept {
		return m_Connections.contains(connection);
	}

	bool ConnectionManager::MarkTransportClosed(Network::ConnectionId connection, ClientCloseCause cause) noexcept {
		auto* client = Find(connection);
		if (client == nullptr) return false;
		client->MarkTransportClosed(cause);
		return true;
	}

	bool ConnectionManager::Remove(Network::ConnectionId connection) noexcept {
		const auto iterator = m_Connections.find(connection);
		if (iterator == m_Connections.end()) return false;
		(void)ReleaseIdentity(connection);
		m_Connections.erase(iterator);
		return true;
	}

	std::size_t ConnectionManager::AuthenticatedCount() const noexcept {
		return m_UsernameIndex.size();
	}

	std::vector<ClientTimeout> ConnectionManager::CollectTimeouts(TimePoint now) const {
		std::vector<ClientTimeout> timeouts;
		timeouts.reserve(m_Connections.size());
		for (const auto& [connection, client] : m_Connections) {
			(void)connection;
			auto timeout = client.CheckTimeout(m_Config, now);
			if (timeout.has_value()) timeouts.push_back(*timeout);
		}
		return timeouts;
	}

	std::vector<ClientSnapshot> ConnectionManager::GetSnapshots() const {
		std::vector<ClientSnapshot> snapshots;
		snapshots.reserve(m_Connections.size());
		for (const auto& [connection, client] : m_Connections) {
			(void)connection;
			snapshots.push_back(client.GetSnapshot());
		}
		return snapshots;
	}

	void ConnectionManager::MarkAllClosing(ClientCloseCause cause) noexcept {
		for (auto& [connection, client] : m_Connections) {
			(void)connection;
			client.MarkCloseRequested(cause);
		}
	}

	void ConnectionManager::Clear() noexcept {
		m_UsernameIndex.clear();
		m_UniqueIdIndex.clear();
		m_Connections.clear();
	}

	std::size_t ConnectionManager::Size() const noexcept {
		return m_Connections.size();
	}

	bool ConnectionManager::Empty() const noexcept {
		return m_Connections.empty();
	}

	const ClientConfiguration& ConnectionManager::GetConfiguration() const noexcept {
		return m_Config;
	}

	bool ConnectionManager::SetConfiguration(ClientConfiguration config) noexcept {
		if (!ValidateConfiguration(config)) return false;
		if (config.MaximumPlayers < AuthenticatedCount()) return false;
		if (config.MaximumClients < m_Connections.size()) return false;
		m_Config = config;
		return true;
	}

	bool ConnectionManager::ValidateConfiguration(const ClientConfiguration& config) noexcept {
		if (config.MaximumClients == 0) return false;
		if (config.MaximumPlayers == 0) return false;
		if (config.HandshakeTimeout <= std::chrono::milliseconds::zero()) return false;
		if (config.LoginTimeout <= std::chrono::milliseconds::zero()) return false;
		if (config.ConfigurationTimeout <= std::chrono::milliseconds::zero()) return false;
		if (config.IdleTimeout <= std::chrono::milliseconds::zero()) return false;
		if (config.ProtocolPipeline.MaximumFramedPacketSize == 0) return false;
		if (config.ProtocolPipeline.MaximumDecompressedPacketSize == 0) return false;
		if (config.ProtocolPipeline.MaximumOutboundActionBytes == 0) return false;
		return true;
	}

	std::string ConnectionManager::NormalizeUsername(std::string_view username) {
		std::string result(username);
		for (auto& character : result) {
			if (character >= 'A' && character <= 'Z')
				character = static_cast<char>(character - 'A' + 'a');
		}
		return result;
	}
}

