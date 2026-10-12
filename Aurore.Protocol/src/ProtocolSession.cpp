#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <format>
#include <limits>
#include <type_traits>
#include <utility>

namespace Aurore::Protocol {
	namespace Configuration = Packets::Configuration;
	namespace Play = Packets::Play;

	ProtocolSession::ProtocolSession(ServerStatus server_status)
		: m_ServerStatus(std::move(server_status)) {}

	ProtocolHandleResult ProtocolSession::HandlePacket(const PacketFrame& frame) {
		switch (m_State) {
		case ProtocolState::Handshake: return HandleHandshakeFrame(frame);
		case ProtocolState::Status: return HandleStatusFrame(frame);
		case ProtocolState::Login: return HandleLoginFrame(frame);
		case ProtocolState::Configuration: return HandleConfigurationFrame(frame);
		case ProtocolState::Play: return HandlePlayFrame(frame);
		case ProtocolState::Disconnected: return std::unexpected(ProtocolError::UnexpectedPacket);
		}
		return std::unexpected(ProtocolError::UnsupportedState);
	}

	ProtocolHandleResult ProtocolSession::ResolveLogin(const LoginResolution& resolution) {
		return std::visit([this](const auto& value) { return ResolveLogin(value); }, resolution);
	}

	void ProtocolSession::Disconnect() noexcept {
		m_PendingRequestId.reset();
		m_ConfigurationPlan.reset();
		m_ClientInformation.reset();
		m_ClientBrand.reset();
		m_RegistrySnapshot.reset();
		m_ConfigurationPhase = ConfigurationPhase::Complete;
		m_State = ProtocolState::Disconnected;
	}

	ProtocolState ProtocolSession::GetState() const noexcept { return m_State; }
	bool ProtocolSession::IsDisconnected() const noexcept { return m_State == ProtocolState::Disconnected; }
	bool ProtocolSession::HasHandshake() const noexcept { return m_Handshake.has_value(); }

	const HandshakeData* ProtocolSession::GetHandshake() const noexcept {
		return m_Handshake ? &*m_Handshake : nullptr;
	}

	const ServerStatus& ProtocolSession::GetServerStatus() const noexcept { return m_ServerStatus; }
	const std::optional<ProtocolRequestId>& ProtocolSession::GetPendingRequestId() const noexcept { return m_PendingRequestId; }
	const std::optional<Packets::Configuration::Serverbound::ClientInformation>& ProtocolSession::GetClientInformation() const noexcept { return m_ClientInformation; }
	const std::optional<std::string>& ProtocolSession::GetClientBrand() const noexcept { return m_ClientBrand; }

	Aurore::Util::RegistryGeneration ProtocolSession::GetConfigurationGeneration() const noexcept {
		if (m_RegistrySnapshot) return m_RegistrySnapshot->GetGeneration();
		if (m_ConfigurationPlan) return m_ConfigurationPlan->GetGeneration();
		return Aurore::Util::NoRegistryGeneration;
	}

	const std::shared_ptr<const Aurore::Util::RegistrySnapshot>& ProtocolSession::GetRegistrySnapshot() const noexcept {
		return m_RegistrySnapshot;
	}

	ProtocolHandleResult ProtocolSession::HandleHandshakeFrame(const PacketFrame& frame) {
		auto packet = Packets::Handshake::Serverbound::Decode(frame);
		if (!packet) return std::unexpected(packet.error());
		return std::visit([this](const auto& value) { return HandleHandshakePacket(value); }, *packet);
	}

	ProtocolHandleResult ProtocolSession::HandleStatusFrame(const PacketFrame& frame) {
		auto packet = Packets::Status::Serverbound::Decode(frame);
		if (!packet) return std::unexpected(packet.error());
		return std::visit([this](const auto& value) { return HandleStatusPacket(value); }, *packet);
	}

	ProtocolHandleResult ProtocolSession::HandleLoginFrame(const PacketFrame& frame) {
		auto packet = Packets::Login::Serverbound::Decode(frame);
		if (!packet) return std::unexpected(packet.error());
		return std::visit([this](const auto& value) { return HandleLoginPacket(value); }, *packet);
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationFrame(const PacketFrame& frame) {
		auto packet = Configuration::Serverbound::Decode(frame);
		if (!packet) return std::unexpected(packet.error());
		return std::visit([this](const auto& value) { return HandleConfigurationPacket(value); }, *packet);
	}

	ProtocolHandleResult ProtocolSession::HandlePlayFrame(const PacketFrame& frame) {
		auto packet = Play::Serverbound::Decode(frame);
		if (!packet) return std::unexpected(packet.error());
		return std::visit([this](const auto& value) { return HandlePlayPacket(value); }, *packet);
	}

	ProtocolHandleResult ProtocolSession::HandleHandshakePacket(const Packets::Handshake::Serverbound::Handshake& packet) {
		ProtocolAction action;

		switch (packet.Intention) {
		case HandshakeIntention::Status:
			m_Handshake = packet;
			m_State = ProtocolState::Status;
			return action;

		case HandshakeIntention::Login:
			m_Handshake = packet;
			if (packet.ProtocolVersion != m_ServerStatus.ProtocolVersion) {
				action.OutboundPackets.emplace_back(Packets::Login::Clientbound::Disconnect{
					.ReasonJson = std::format(
						R"({{"text":"Unsupported protocol version. Aurore requires protocol {}."}})",
						m_ServerStatus.ProtocolVersion),
				});
				action.Disconnect = true;
				m_LoginPhase = LoginPhase::Complete;
				m_State = ProtocolState::Disconnected;
				return action;
			}
			m_State = ProtocolState::Login;
			return action;
		}

		return std::unexpected(ProtocolError::InvalidNextState);
	}

	ProtocolHandleResult ProtocolSession::HandleStatusPacket(const Packets::Status::Serverbound::Request& packet) {
		(void)packet;
		if (m_StatusPhase != StatusPhase::AwaitingRequest)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Status::Clientbound::Response{ .Status = m_ServerStatus });
		m_StatusPhase = StatusPhase::AwaitingPing;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleStatusPacket(const Packets::Status::Serverbound::Ping& packet) {
		if (m_StatusPhase != StatusPhase::AwaitingPing)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Status::Clientbound::Pong{ .Payload = packet.Payload });
		action.Disconnect = true;
		m_StatusPhase = StatusPhase::Complete;
		m_State = ProtocolState::Disconnected;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleLoginPacket(const Packets::Login::Serverbound::Start& packet) {
		if (m_LoginPhase != LoginPhase::AwaitingStart)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		const auto request_id = AllocateRequestId();
		ProtocolAction action;
		action.Requests.emplace_back(LoginStartRequest{
			.Id = request_id,
			.Username = packet.Username,
			.PresentedProfileId = packet.PresentedProfileId,
		});
		action.RequireInboundBoundary = true;
		m_PendingRequestId = request_id;
		m_LoginPhase = LoginPhase::AwaitingCoreDecision;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleLoginPacket(const Packets::Login::Serverbound::Acknowledged& packet) {
		(void)packet;
		if (m_LoginPhase != LoginPhase::AwaitingAcknowledgement)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		const auto request_id = AllocateRequestId();
		ProtocolAction action;
		action.Requests.emplace_back(ConfigurationStartRequest{ .Id = request_id });
		m_PendingRequestId = request_id;
		m_LoginPhase = LoginPhase::Complete;
		m_ConfigurationPhase = ConfigurationPhase::AwaitingBootstrap;
		m_State = ProtocolState::Configuration;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::ClientInformation& packet) {
		if (m_ConfigurationPhase == ConfigurationPhase::Complete)
			return std::unexpected(ProtocolError::UnexpectedPacket);
		m_ClientInformation = packet;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::ClientBrand& packet) {
		if (m_ConfigurationPhase == ConfigurationPhase::Complete)
			return std::unexpected(ProtocolError::UnexpectedPacket);
		m_ClientBrand = packet.Brand;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::CustomPayload& packet) {
		if (m_ConfigurationPhase == ConfigurationPhase::Complete)
			return std::unexpected(ProtocolError::UnexpectedPacket);
		(void)packet;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::SelectKnownPacks& packet) {
		if (m_ConfigurationPhase != ConfigurationPhase::AwaitingKnownPackSelection)
			return std::unexpected(ProtocolError::UnexpectedPacket);
		if (!m_ConfigurationPlan)
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		const auto selection = Configuration::ValidateKnownPackSelection(
			m_ConfigurationPlan->GetOfferedKnownPacks(), packet.Packs);
		if (!selection)
			return std::unexpected(ProtocolError::InvalidKnownPackSelection);

		auto frames = m_ConfigurationPlan->MaterializePostNegotiationFrames(packet.Packs);
		if (!frames)
			return std::unexpected(ProtocolError::ConfigurationMaterializationFailed);
		if (frames->empty())
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		ProtocolAction action;
		action.OutboundPackets.reserve(frames->size());
		for (auto& frame : *frames)
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(frame) });

		m_ConfigurationPhase = ConfigurationPhase::AwaitingFinishAcknowledgement;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::FinishConfiguration& packet) {
		(void)packet;
		if (m_ConfigurationPhase != ConfigurationPhase::AwaitingFinishAcknowledgement)
			return std::unexpected(ProtocolError::UnexpectedPacket);
		if (!m_ConfigurationPlan)
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		const auto generation = m_ConfigurationPlan->GetGeneration();
		if (generation == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(ProtocolError::ConfigurationGenerationMismatch);

		const auto request_id = AllocateRequestId();
		ProtocolAction action;
		action.Requests.emplace_back(ConfigurationCompleteRequest{ .Id = request_id, .Generation = generation });
		action.RequireInboundBoundary = true;
		m_PendingRequestId = request_id;
		m_ConfigurationPhase = ConfigurationPhase::AwaitingPlayAdmission;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::AcceptTeleportation& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(TeleportAcknowledgedRequest{
			.TeleportId = packet.TeleportId,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::ChunkBatchReceived& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(ChunkBatchFeedbackRequest{
			.ChunksPerTick = packet.ChunksPerTick,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::ClientTickEnd& packet) {
		(void)packet;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::KeepAlive& packet) {
		(void)packet;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::MovePlayerPosition& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(PlayerMovementRequest{
			.X = packet.X,
			.Y = packet.Y,
			.Z = packet.Z,
			.OnGround = packet.Flags.OnGround,
			.HorizontalCollision = packet.Flags.HorizontalCollision,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::MovePlayerPositionRotation& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(PlayerMovementRequest{
			.X = packet.X,
			.Y = packet.Y,
			.Z = packet.Z,
			.Yaw = packet.Yaw,
			.Pitch = packet.Pitch,
			.OnGround = packet.Flags.OnGround,
			.HorizontalCollision = packet.Flags.HorizontalCollision,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::MovePlayerRotation& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(PlayerMovementRequest{
			.Yaw = packet.Yaw,
			.Pitch = packet.Pitch,
			.OnGround = packet.Flags.OnGround,
			.HorizontalCollision = packet.Flags.HorizontalCollision,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::MovePlayerStatusOnly& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(PlayerMovementRequest{
			.OnGround = packet.Flags.OnGround,
			.HorizontalCollision = packet.Flags.HorizontalCollision,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::PlayerInput& packet) {
		ProtocolAction action;
		action.Requests.emplace_back(PlayerInputRequest{
			.Flags = packet.Flags,
		});
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Play::Serverbound::PlayerLoaded& packet) {
		(void)packet;
		ProtocolAction action;
		action.Requests.emplace_back(PlayerLoadedRequest{});
		return action;
	}

	// First Light recognizes ordinary client intent, but doesn't yet implement
	// gameplay, inventories, entity animation, or authoritative sprint physics.
	// Those effects belong to Alpha 1, not an accidental pseudo-simulation.
	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::PlayerAbilities& packet) {
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::PlayerCommand& packet) {
		return ProtocolAction{};
	}
	
	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::SetCarriedItem& packet) {
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::ContainerClose& packet) {
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::Swing& packet) {
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandlePlayPacket(const Packets::Play::Serverbound::DeferredInteraction& packet) {
		ProtocolAction action;
		if (packet.Sequence.has_value()) 
			action.OutboundPackets.emplace_back(PreparedPacketFrame{
				.Frame = Play::Clientbound::Encode(Play::Clientbound::BlockChangedAck{
					.Sequence = *packet.Sequence,
				}),
			});
		return action;
	}


	ProtocolHandleResult ProtocolSession::ResolveLogin(const LoginAcceptedResolution& resolution) {
		if (!MatchesPendingLoginRequest(resolution.Id) || resolution.Username.empty() || resolution.ProfileId.IsNil())
			return std::unexpected(ProtocolError::InvalidRequestResolution);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Login::Clientbound::Success{
			.ProfileId = resolution.ProfileId,
			.Username = resolution.Username,
			.Properties = resolution.Properties,
		});
		m_PendingRequestId.reset();
		m_LoginPhase = LoginPhase::AwaitingAcknowledgement;
		return action;
	}

	ProtocolHandleResult ProtocolSession::ResolveLogin(const LoginRejectedResolution& resolution) {
		if (!MatchesPendingLoginRequest(resolution.Id) || resolution.ReasonJson.empty())
			return std::unexpected(ProtocolError::InvalidRequestResolution);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Login::Clientbound::Disconnect{ .ReasonJson = resolution.ReasonJson });
		action.Disconnect = true;
		m_PendingRequestId.reset();
		m_LoginPhase = LoginPhase::Complete;
		m_State = ProtocolState::Disconnected;
		return action;
	}

	ProtocolHandleResult ProtocolSession::ResolveConfiguration(ConfigurationResolution resolution) {
		return std::visit([this](auto&& value) -> ProtocolHandleResult {
			using Resolution = std::remove_cvref_t<decltype(value)>;
			if constexpr (std::is_same_v<Resolution, ConfigurationReadyResolution>)
				return ResolveConfigurationReady(std::move(value));
			else if constexpr (std::is_same_v<Resolution, ConfigurationAcceptedResolution>)
				return ResolveConfigurationAccepted(value);
			else
				return ResolveConfigurationRejected(value);
		}, std::move(resolution));
	}

	ProtocolHandleResult ProtocolSession::ResolveConfigurationReady(ConfigurationReadyResolution&& resolution) {
		if (!MatchesPendingConfigurationRequest(resolution.Id, ConfigurationPhase::AwaitingBootstrap))
			return std::unexpected(ProtocolError::InvalidRequestResolution);
		if (!resolution.Plan.GetSnapshot() || resolution.Plan.GetGeneration() == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(ProtocolError::ConfigurationGenerationMismatch);

		auto frames = resolution.Plan.ReleaseInitialFrames();
		if (frames.size() != 2
			|| frames[0].PacketId != Configuration::Clientbound::FeatureFlagsPacketId
			|| frames[1].PacketId != Configuration::Clientbound::SelectKnownPacksPacketId)
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		ProtocolAction action;
		action.OutboundPackets.reserve(frames.size());
		for (auto& frame : frames)
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(frame) });

		m_ConfigurationPlan.emplace(std::move(resolution.Plan));
		m_PendingRequestId.reset();
		m_ConfigurationPhase = ConfigurationPhase::AwaitingKnownPackSelection;
		return action;
	}

	ProtocolHandleResult ProtocolSession::ResolveConfigurationAccepted(const ConfigurationAcceptedResolution& resolution) {
		if (!MatchesPendingConfigurationRequest(resolution.Id, ConfigurationPhase::AwaitingPlayAdmission))
			return std::unexpected(ProtocolError::InvalidRequestResolution);
		if (!m_ConfigurationPlan || !m_ConfigurationPlan->GetSnapshot())
			return std::unexpected(ProtocolError::MissingConfigurationPlan);
		if (resolution.Admission.PlayerEntityId <= 0 || resolution.Admission.DimensionIdentifier.empty())
			return std::unexpected(ProtocolError::PlayBootstrapFailed);

		m_RegistrySnapshot = m_ConfigurationPlan->GetSnapshot();
		if (m_RegistrySnapshot->GetGeneration() != m_ConfigurationPlan->GetGeneration()) {
			m_RegistrySnapshot.reset();
			return std::unexpected(ProtocolError::ConfigurationGenerationMismatch);
		}

		auto dimension = Aurore::Util::ResourceLocation::Parse(resolution.Admission.DimensionIdentifier);
		if (!dimension) {
			m_RegistrySnapshot.reset();
			return std::unexpected(ProtocolError::PlayBootstrapFailed);
		}

		const auto& dimension_types = m_RegistrySnapshot->GetDimensionTypes();
		auto dimension_type_id = dimension_types.FindRuntimeId(*dimension);
		if (!dimension_type_id && dimension_types.Size() == 1)
			dimension_type_id = dimension_types.GetEntries().front().RuntimeId;
		if (!dimension_type_id) {
			m_RegistrySnapshot.reset();
			return std::unexpected(ProtocolError::PlayBootstrapFailed);
		}

		constexpr std::int32_t FirstLightChunkRadius{ 2 };
		constexpr std::int32_t FirstLightSimulationDistance{ 2 };
		constexpr std::int32_t FirstLightTeleportId{ 1 };

		ProtocolAction action;
		action.OutboundPackets.reserve(7u + resolution.Admission.InitialChunks.size());

		auto login = Play::Clientbound::Encode(Play::Clientbound::Login{
			.PlayerId = resolution.Admission.PlayerEntityId,
			.Hardcore = false,
			.Levels = { *dimension },
			.MaximumPlayers = m_ServerStatus.MaximumPlayers,
			.ChunkRadius = FirstLightChunkRadius,
			.SimulationDistance = FirstLightSimulationDistance,
			.ReducedDebugInfo = false,
			.ShowDeathScreen = true,
			.LimitedCrafting = false,
			.SpawnInfo = Play::Clientbound::CommonSpawnInfo{
				.DimensionType = *dimension_type_id,
				.Dimension = *dimension,
				.Seed = 0,
				.GameModeValue = Play::GameMode::Creative,
				.PreviousGameMode = -1,
				.IsDebug = false,
				.IsFlat = false,
				.HasLastDeathLocation = false,
				.PortalCooldown = 0,
				.SeaLevel = resolution.Admission.SeaLevel,
			},
			.EnforcesSecureChat = false,
		});

		auto position = Play::Clientbound::Encode(Play::Clientbound::PlayerPosition{
			.TeleportId = FirstLightTeleportId,
			.X = resolution.Admission.X,
			.Y = resolution.Admission.Y,
			.Z = resolution.Admission.Z,
			.VelocityX = 0.0,
			.VelocityY = 0.0,
			.VelocityZ = 0.0,
			.Yaw = resolution.Admission.Yaw,
			.Pitch = resolution.Admission.Pitch,
			.RelativeFlags = 0,
		});

		auto start_waiting = Play::Clientbound::Encode(
			Play::Clientbound::GameEvent{
				.Event = Play::Clientbound::GameEventType::StartWaitingForLevelChunks,
				.Value = 0.0F,
			});

		action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(login) });
		action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(start_waiting) });
		action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(position) });

		if (!resolution.Admission.InitialChunks.empty()) {
			const auto& columns = resolution.Admission.InitialChunks;

			if (columns.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				m_RegistrySnapshot.reset();
				return std::unexpected(ProtocolError::PlayBootstrapFailed);
			}

			std::vector<PacketFrame> chunks;
			chunks.reserve(columns.size());
			for (const auto& column : columns) {
				if (m_RegistrySnapshot->GetBlocks().Find(column.AirBlock) == nullptr) {
					m_RegistrySnapshot.reset();
					return std::unexpected(ProtocolError::PlayBootstrapFailed);
				}

				auto chunk = EncodeLevelChunkWithLight(column, *m_RegistrySnapshot);
				if (!chunk) {
					m_RegistrySnapshot.reset();
					return std::unexpected(chunk.error());
				}
				chunks.push_back(std::move(*chunk));
			}

			/* The last column is the spawn column; its neighbors precede it. */
			const auto& center = columns.back();
			auto cache_radius = Play::Clientbound::Encode(
				Play::Clientbound::SetChunkCacheRadius{ .Radius = FirstLightChunkRadius });
			auto cache_center = Play::Clientbound::Encode(
				Play::Clientbound::SetChunkCacheCenter{
					.ChunkX = center.X,
					.ChunkZ = center.Z,
				});
			auto batch_start = Play::Clientbound::Encode(Play::Clientbound::ChunkBatchStart{});
			auto batch_finished = Play::Clientbound::Encode(
				Play::Clientbound::ChunkBatchFinished{
					.BatchSize = static_cast<std::int32_t>(chunks.size()),
				});

			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(cache_radius) });
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(cache_center) });
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(batch_start) });
			for (auto& chunk : chunks)
				action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(chunk) });
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(batch_finished) });
		}

		m_ConfigurationPlan.reset();
		m_PendingRequestId.reset();
		m_ConfigurationPhase = ConfigurationPhase::Complete;
		m_State = ProtocolState::Play;
		return action;
	}

	ProtocolHandleResult ProtocolSession::ResolveConfigurationRejected(const ConfigurationRejectedResolution& resolution) {
		if (!MatchesPendingConfigurationRequest(resolution.Id, ConfigurationPhase::AwaitingPlayAdmission))
			return std::unexpected(ProtocolError::InvalidRequestResolution);

		ProtocolAction action;
		action.Disconnect = true;
		m_PendingRequestId.reset();
		m_ConfigurationPlan.reset();
		m_RegistrySnapshot.reset();
		m_ConfigurationPhase = ConfigurationPhase::Complete;
		m_State = ProtocolState::Disconnected;
		return action;
	}

	bool ProtocolSession::MatchesPendingLoginRequest(ProtocolRequestId id) const noexcept {
		return id && m_State == ProtocolState::Login && m_LoginPhase == LoginPhase::AwaitingCoreDecision
			&& m_PendingRequestId.has_value() && *m_PendingRequestId == id;
	}

	bool ProtocolSession::MatchesPendingConfigurationRequest(ProtocolRequestId id, ConfigurationPhase phase) const noexcept {
		return id && m_State == ProtocolState::Configuration && m_ConfigurationPhase == phase
			&& m_PendingRequestId.has_value() && *m_PendingRequestId == id;
	}

	ProtocolRequestId ProtocolSession::AllocateRequestId() noexcept {
		return ProtocolRequestId{ m_NextRequestValue++ };
	}
}
