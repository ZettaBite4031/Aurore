#include <Aurore/Core/ClientService.hpp>

#include <Aurore/Core/logger/Log.hpp>

#include <Aurore/Protocol/ProtocolDiagnostics.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
	std::string_view ClientDispositionName(Aurore::Core::ClientReceiveDisposition disposition) noexcept {
		using Aurore::Core::ClientReceiveDisposition;

		switch (disposition) {
		case ClientReceiveDisposition::KeepOpen: return "KeepOpen";
		case ClientReceiveDisposition::CloseAfterFlush: return "CloseAfterFlush";
		case ClientReceiveDisposition::CloseImmediately: return "CloseImmediately";
		}
		return "Unknown";
	}

	std::size_t TotalOutboundBytes(const Aurore::Core::ClientReceiveResult& result) noexcept {
		std::size_t total{ 0 };
		for (const auto& batch : result.OutboundFrames)
			total += batch.Size();
		return total;
	}
}

namespace Aurore::Core {
	ClientService::ClientService(Network::NetworkManager& network_manager,
		RegistrySnapshotStore& registry_snapshots) noexcept
		: m_NetworkManager(network_manager), m_RegistrySnapshots(registry_snapshots) {}

	bool ClientService::Initialize(ClientConfiguration client_configuration,
		Protocol::ConfigurationSequencePolicy configuration_policy,
		Protocol::Packets::Configuration::Limits configuration_limits,
		Protocol::ServerStatus server_status) {

		if (!m_ConnectionManager.SetConfiguration(std::move(client_configuration)))
			return false;

		m_ConfigurationPolicy = std::move(configuration_policy);
		m_ConfigurationLimits = configuration_limits;
		m_ServerStatus = std::move(server_status);
		m_FatalNetworkFailure = false;

		m_ConnectionManager.Clear();
		return true;
	}

	Protocol::ServerStatus ClientService::BuildServerStatus() const {
		auto status = m_ServerStatus;
		const auto online_players = m_ConnectionManager.AuthenticatedCount();

		status.OnlinePlayers = static_cast<std::int32_t>(std::min<std::size_t>(
			online_players,
			static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));

		return status;
	}

	bool ClientService::ProcessNetworkEvents() {
		auto events = m_NetworkManager.DrainEvents();
		for (auto& event : events) {
			std::visit([this](const auto& value) {
				HandleNetworkEvent(value);
			}, event);
		}
		return !m_FatalNetworkFailure;
	}

	void ClientService::HandleNetworkEvent(const Network::ConnectionOpenedEvent& event) {
		auto open_result = m_ConnectionManager.Open(event.Connection, event.LocalEndpoint, event.RemoteEndpoint, BuildServerStatus());
		if (!open_result) {
			const auto error = open_result.error();
			if (error == ClientError::CapacityExceeded) {
				AU_WARN(LogCategory::Core, "Rejected connection from {}:{} because client capacity was reached",
					event.RemoteEndpoint.Address, event.RemoteEndpoint.Port);
			}
			else {
				AU_ERROR(LogCategory::Core, "Failed to register connection {} from {}:{}. Client error: {}",
					event.Connection.Value, event.RemoteEndpoint.Address, event.RemoteEndpoint.Port,
					static_cast<unsigned int>(error));
			}

			const auto close_result = m_NetworkManager.CloseImmediately(event.Connection,
				error == ClientError::CapacityExceeded
					? Network::ConnectionCloseReason::ApplicationRequested
					: Network::ConnectionCloseReason::BackendFailure);
			if (!close_result) {
				AU_ERROR(LogCategory::Core, "Failed to close rejected connection {}. Network error: {}",
					event.Connection.Value, static_cast<unsigned int>(close_result.error()));
			}
			return;
		}
		const auto& client = open_result->get();
		AU_DEBUG(LogCategory::Core, "Accepted connection {} from {}:{}", client.GetConnectionId().Value,
			client.GetRemoteEndpoint().Address, client.GetRemoteEndpoint().Port);
	}

	void ClientService::HandleNetworkEvent(const Network::BytesReceivedEvent& event) {
		auto* client = m_ConnectionManager.Find(event.Connection);
		if (client == nullptr) {
			AU_ERROR(LogCategory::Core, "Received network data for unknown connection {}", event.Connection.Value);
			const auto close_result = m_NetworkManager.CloseImmediately(event.Connection, Network::ConnectionCloseReason::BackendFailure);
			if (!close_result) {
				AU_ERROR(LogCategory::Core, "Failed to close unknown connection {}. Error: {}",
					event.Connection.Value, static_cast<unsigned int>(close_result.error()));
			}
			return;
		}
		if (client->IsCloseRequested() || !client->IsTransportOpen()) {
			AU_WARN(LogCategory::Core, "Received data for connection {} after closure was requested", event.Connection.Value);
			const auto close_result = m_NetworkManager.CloseImmediately(event.Connection, Network::ConnectionCloseReason::BackendFailure);
			if (!close_result) {
				AU_ERROR(LogCategory::Core, "Failed to enforce closure for connection {}. Error: {}",
					event.Connection.Value, static_cast<unsigned int>(close_result.error()));
			}
			return;
		}

		const auto state_before = client->GetProtocolState();
		const auto buffered_before = client->GetProtocolConnection().BufferedBytes();
		AU_TRACE(LogCategory::Protocol,
			"Processing {} inbound transport bytes for connection {}. State: {}, buffered before: {}",
			event.Data.size(), event.Connection.Value, Protocol::GetProtocolStateName(state_before), buffered_before);

		const auto bytes = std::span<const std::byte>{ event.Data.data(), event.Data.size() };
		auto receive_result = client->Receive(bytes);
		const auto state_after = client->GetProtocolState();
		const auto buffered_after = client->GetProtocolConnection().BufferedBytes();

		AU_TRACE(LogCategory::Protocol,
			"Protocol receive completed for connection {}. State: {} -> {}, Buffered Bytes: {} -> {}, Requests: {}, Outbound Batches: {}, Outbound Bytes: {}, Disposition: {}, Failed: {}",
			event.Connection.Value, Protocol::GetProtocolStateName(state_before), Protocol::GetProtocolStateName(state_after), buffered_before, buffered_after,
			receive_result.Requests.size(), receive_result.OutboundFrames.size(), TotalOutboundBytes(receive_result), ClientDispositionName(receive_result.Disposition), receive_result.HasError());

		auto requests = std::move(receive_result.Requests);
		if (!ApplyClientResult(*client, std::move(receive_result))) return;
		for (const auto& request : requests)
			if (!HandleProtocolRequest(*client, request)) return;

		const auto resume_result = m_NetworkManager.ResumeReceive(event.Connection);
		if (resume_result) return;

		AU_ERROR(LogCategory::Core, "Failed to resume receive for connection {}. Error: {}",
			event.Connection.Value, static_cast<unsigned int>(resume_result.error()));

		client->MarkCloseRequested(ClientCloseCause::TransportFailure);
		const auto close_result = m_NetworkManager.CloseImmediately(event.Connection, Network::ConnectionCloseReason::BackendFailure);
		if (!close_result) {
			AU_ERROR(LogCategory::Core, "Failed to close connection {} after receive-resume failure. Error: {}",
				event.Connection.Value, static_cast<unsigned int>(close_result.error()));
		}
	}

	void ClientService::HandleNetworkEvent(const Network::ConnectionClosedEvent& event) {
		auto* client = m_ConnectionManager.Find(event.Connection);
		if (client == nullptr) {
			AU_WARN(LogCategory::Core, "Received close event for unknown connection {}: {}", event.Connection.Value, event.Detail);
			return;
		}

		ClientCloseCause cause = ClientCloseCause::TransportClosed;
		switch (event.Reason) {
		case Network::ConnectionCloseReason::RemoteClosed:
		case Network::ConnectionCloseReason::ApplicationRequested:
		case Network::ConnectionCloseReason::IdleTimeout:
			cause = ClientCloseCause::TransportClosed;
			break;
		case Network::ConnectionCloseReason::ServerStopping:
			cause = ClientCloseCause::ServerStopping;
			break;
		case Network::ConnectionCloseReason::TransportError:
		case Network::ConnectionCloseReason::InboundLimitExceeded:
		case Network::ConnectionCloseReason::OutboundLimitExceeded:
		case Network::ConnectionCloseReason::BackendFailure:
			cause = ClientCloseCause::TransportFailure;
			break;
		}

		client->MarkTransportClosed(cause);
		AU_DEBUG(LogCategory::Core, "Connection {} from {}:{} closed: {}",
			event.Connection.Value, client->GetRemoteEndpoint().Address,
			client->GetRemoteEndpoint().Port, event.Detail);
		(void)m_ConnectionManager.Remove(event.Connection);
	}

	void ClientService::HandleNetworkEvent(const Network::NetworkFailureEvent& event) {
		AU_ERROR(LogCategory::Core, "Network failure: {} (error code: {})", event.Message, static_cast<unsigned int>(event.Error));
		if (event.Fatal) m_FatalNetworkFailure = true;
	}

	bool ClientService::ApplyClientResult(ClientConnection& client, ClientReceiveResult result) {
		const auto connection = client.GetConnectionId();
		if (result.HasError()) {
			const auto error_name =
				Protocol::GetProtocolConnectionErrorName(*result.ProtocolError);

			if (result.FailureContext) {
				const auto& context = *result.FailureContext;

				AU_WARN(LogCategory::Protocol,
					"Protocol processing failed for connection {}. Stage: {}, state: {}, packet: {}, packet payload bytes: {}, input bytes: {}, buffered inbound bytes: {}, error: {}",
					connection.Value, Protocol::GetProtocolStageName(context.Stage), Protocol::GetProtocolStateName(context.State), Protocol::FormatPacketId(context.PacketId), context.PacketPayloadBytes,
					context.InputBytes, context.BufferedInboundBytes, error_name);
			}
			else {
				AU_WARN(LogCategory::Protocol,
					"Protocol processing failed for connection {} without failure context. Error: {}",
					connection.Value, error_name);
			}
		}

		for (auto& frame : result.OutboundFrames) {
			const auto frame_size = frame.Size();
			const auto send_result = m_NetworkManager.QueueSend(connection, std::move(frame));
			if (send_result) {
				AU_TRACE(LogCategory::Protocol,
					"Queued {} protocol bytes for connection {}",
					frame_size, connection.Value);
				continue;
			}

			AU_ERROR(LogCategory::Core, "Failed to queue protocol output for connection {}. Error: {}",
				connection.Value, static_cast<unsigned int>(send_result.error()));

			client.MarkCloseRequested(ClientCloseCause::TransportFailure);
			const auto close_result = m_NetworkManager.CloseImmediately(connection, Network::ConnectionCloseReason::BackendFailure);
			if (!close_result)
				AU_ERROR(LogCategory::Core, "Failed to close connection {} after send failure. Error: {}",
					connection.Value, static_cast<unsigned int>(close_result.error()));
			return false;
		}

		if (result.Disposition == ClientReceiveDisposition::KeepOpen) return !result.HasError();
		RequestClientClose(client, result.Disposition, result.HasError() ? ClientCloseCause::ProtocolFailure : ClientCloseCause::ProtocolRequested);
		return false;
	}

	void ClientService::RequestClientClose(ClientConnection& client, ClientReceiveDisposition disposition, ClientCloseCause cause) {
		const auto connection = client.GetConnectionId();
		client.MarkCloseRequested(cause);

		Network::NetworkResult<void> close_result = std::unexpected(Network::NetworkError::BackendFailure);
		switch (disposition) {
		case ClientReceiveDisposition::KeepOpen:
			return;
		case ClientReceiveDisposition::CloseAfterFlush:
			close_result = m_NetworkManager.CloseAfterFlush(connection);
			break;
		case ClientReceiveDisposition::CloseImmediately:
			close_result = m_NetworkManager.CloseImmediately(connection);
			break;
		}

		if (close_result) return;

		AU_ERROR(LogCategory::Core, "Failed to request closure for connection {}. Error: {}",
			connection.Value, static_cast<unsigned int>(close_result.error()));

		if (disposition == ClientReceiveDisposition::CloseImmediately) return;

		const auto immediate_result = m_NetworkManager.CloseImmediately(connection, Network::ConnectionCloseReason::BackendFailure);
		if (!immediate_result)
			AU_ERROR(LogCategory::Core, "Failed to immediately close connection {} after graceful-close failure. Error: {}",
				connection.Value, static_cast<unsigned int>(immediate_result.error()));
	}

	void ClientService::ProcessTimeouts(ClientConnection::TimePoint now) {
		const auto timeouts = m_ConnectionManager.CollectTimeouts(now);
		for (const auto& timeout : timeouts) {
			auto* client = m_ConnectionManager.Find(timeout.Connection);
			if (client == nullptr) continue;
			AU_WARN(LogCategory::Core, "Closing timed-out client {} from {}:{}",
				timeout.Connection.Value,
				client->GetRemoteEndpoint().Address,
				client->GetRemoteEndpoint().Port);
			client->MarkCloseRequested(timeout.Cause);

			const auto close_result = m_NetworkManager.CloseImmediately(timeout.Connection, Network::ConnectionCloseReason::IdleTimeout);
			if (!close_result) {
				AU_ERROR(LogCategory::Core, "Failed to close timed-out client {}. Error: {}",
					timeout.Connection.Value, static_cast<unsigned int>(close_result.error()));
			}
		}
	}

	bool ClientService::HandleProtocolRequest(ClientConnection& client, const Protocol::ProtocolRequest& request) {
		return std::visit([this, &client](const auto& value) -> bool {
			using Request = std::remove_cvref_t<decltype(value)>;

			if constexpr (std::is_same_v<Request, Protocol::LoginStartRequest>)
				return HandleLoginStartRequest(client, value);
			else if constexpr (std::is_same_v<Request, Protocol::ConfigurationStartRequest>)
				return HandleConfigurationStartRequest(client, value);
			else {
				static_assert(std::is_same_v<Request, Protocol::ConfigurationCompleteRequest>);
				return HandleConfigurationCompleteRequest(client, value);
			}
		}, request);
	}

	bool ClientService::HandleLoginStartRequest(ClientConnection& client, const Protocol::LoginStartRequest& request) {
		const auto connection = client.GetConnectionId();
		const auto reject_login = [this, &client, &request](std::string reason_json) {
			auto result = client.ResolveLogin(Protocol::LoginRejectedResolution{
				.Id = request.Id,
				.ReasonJson = std::move(reason_json),
			});
			return ApplyClientResult(client, std::move(result));
		};

		auto identity = m_ClientLogin.ResolveOffline(request);
		if (!identity) {
			AU_WARN(LogCategory::Core, "Rejected Login request {} for connection {} because the username was invalid",
				request.Id.Value, connection.Value);
			return reject_login(ClientLogin::BuildDisconnectReason(identity.error()));
		}

		auto admission = m_ConnectionManager.AdmitIdentity(connection, std::move(*identity));
		if (!admission) {
			AU_WARN(LogCategory::Core,
				"Rejected Login request {} for connection {}. Admission error: {}",
				request.Id.Value,
				connection.Value,
				static_cast<unsigned int>(admission.error()));
			return reject_login(ClientLogin::BuildDisconnectReason(admission.error()));
		}

		const auto* accepted_identity = client.GetIdentity() ? &*client.GetIdentity() : nullptr;
		if (accepted_identity == nullptr) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			return reject_login(R"({"text":"Login could not be completed."})");
		}

		auto result = client.ResolveLogin(Protocol::LoginAcceptedResolution{
			.Id = request.Id,
			.ProfileId = accepted_identity->UniqueId,
			.Username = accepted_identity->Username,
			.Properties = accepted_identity->Properties,
		});

		if (result.HasError() || result.Disposition != ClientReceiveDisposition::KeepOpen) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			(void)ApplyClientResult(client, std::move(result));
			return false;
		}

		if (!ApplyClientResult(client, std::move(result))) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			return false;
		}

		AU_INFO(LogCategory::Core, "Accepted offline Login for '{}' ({}) on connection {}",
			accepted_identity->Username, accepted_identity->UniqueId.ToString(), connection.Value);
		return true;
	}

	bool ClientService::HandleConfigurationStartRequest(ClientConnection& client, const Protocol::ConfigurationStartRequest& request) {
		const auto connection = client.GetConnectionId();
		const auto close_failed_configuration = [this, &client, connection]() {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			RequestClientClose(client, ClientReceiveDisposition::CloseImmediately, ClientCloseCause::ProtocolFailure);
			return false;
		};

		const auto snapshot = m_RegistrySnapshots.GetActiveSnapshot();
		if (!snapshot) {
			AU_ERROR(LogCategory::Protocol,
				"Cannot begin Configuration request {} for connection {} because no registry snapshot is active!",
				request.Id.Value, connection.Value);
			return close_failed_configuration();
		}

		auto plan = Protocol::ConfigurationSequenceBuilder::Build(snapshot, m_ConfigurationPolicy, m_ConfigurationLimits);
		if (!plan) {
			const auto& error = plan.error();
			AU_ERROR(LogCategory::Protocol,
				"Failed to build Configuration request {} for connection {}. Code: {}, stage: {}, generation: {}",
				request.Id.Value, connection.Value, static_cast<unsigned int>(error.Code),
				static_cast<unsigned int>(error.Stage), error.Generation);

			if (error.Registry)
				AU_ERROR(LogCategory::Protocol, "Configuration failure registry: {}", static_cast<unsigned int>(*error.Registry));
			if (error.Key)
				AU_ERROR(LogCategory::Protocol, "Configuration failure key: {}", error.Key->ToString());
			return close_failed_configuration();
		}

		const auto generation = plan->GetGeneration();
		auto result = client.ResolveConfiguration(Protocol::ConfigurationResolution{
			Protocol::ConfigurationReadyResolution{
				.Id = request.Id,
				.Plan = std::move(*plan)
			}
		});

		if (result.HasError() || result.Disposition != ClientReceiveDisposition::KeepOpen) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			(void)ApplyClientResult(client, std::move(result));
			return false;
		}

		if (!ApplyClientResult(client, std::move(result))) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			return false;
		}

		AU_DEBUG(LogCategory::Protocol,
			"Started Configuration request {} for connection {} using registry generation {}",
			request.Id.Value, connection.Value, generation);
		return true;
	}

	bool ClientService::HandleConfigurationCompleteRequest(ClientConnection& client, const Protocol::ConfigurationCompleteRequest& request) {
		const auto connection = client.GetConnectionId();
		const auto& session = client.GetProtocolConnection().GetSession();
		const auto& client_brand = session.GetClientBrand();
		const auto& client_information = session.GetClientInformation();
		const auto retained_generation = session.GetConfigurationGeneration();
		const auto& identity = client.GetIdentity();
		const bool has_client_information = session.GetClientInformation().has_value();

		const bool can_enter_play = client.IsTransportOpen() && !client.IsCloseRequested()
			&& identity.has_value() && identity->IsComplete()
			&& has_client_information
			&& client.GetProtocolState() == Protocol::ProtocolState::Configuration
			&& request.Generation != Util::NoRegistryGeneration
			&& retained_generation == request.Generation;

		if (!can_enter_play) {
			AU_WARN(LogCategory::Protocol,
				"Rejected Configuration completion request {} for connection {}. "
				"Requested generation: {}, retained generation: {}, has identity: {}, has client information: {}",
				request.Id.Value, connection.Value, request.Generation,
				retained_generation, identity.has_value(), has_client_information);

			auto result = client.ResolveConfiguration(Protocol::ConfigurationResolution{
				Protocol::ConfigurationRejectedResolution{ .Id = request.Id }
			});

			(void)m_ConnectionManager.ReleaseIdentity(connection);
			(void)ApplyClientResult(client, std::move(result));
			return false;
		}

		auto result = client.ResolveConfiguration(Protocol::ConfigurationResolution{
			Protocol::ConfigurationAcceptedResolution{ .Id = request.Id }
		});

		if (result.HasError() || result.Disposition != ClientReceiveDisposition::KeepOpen) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			(void)ApplyClientResult(client, std::move(result));
			return false;
		}

		if (!ApplyClientResult(client, std::move(result))) {
			(void)m_ConnectionManager.ReleaseIdentity(connection);
			return false;
		}

		AU_INFO(LogCategory::Protocol,
			"Configuration completed for connection {} using registry generation {}",
			connection.Value, request.Generation);

		AU_DEBUG(LogCategory::Protocol,
			"Configuration metadata for connection {}. Client Information: {}, Client Brand: {}, Brand Bytes: {}",
			connection.Value, client_information.has_value(), client_brand.has_value(), client_brand ? client_brand->size() : 0);

		return true;
	}

	void ClientService::BeginShutdown() noexcept {
		m_ConnectionManager.MarkAllClosing(ClientCloseCause::ServerStopping);
	}

	void ClientService::CompleteShutdown() noexcept {
		m_ConnectionManager.Clear();
		m_FatalNetworkFailure = false;
	}

	std::size_t ClientService::GetConnectionCount() const noexcept {
		return m_ConnectionManager.Size();
	}

	ClientConnection* ClientService::FindClient(Network::ConnectionId connection) noexcept {
		return m_ConnectionManager.Find(connection);
	}

	std::vector<ClientSnapshot> ClientService::GetClientSnapshots() const {
		return m_ConnectionManager.GetSnapshots();
	}
}
