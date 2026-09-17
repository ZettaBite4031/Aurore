#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <format>
#include <type_traits>
#include <utility>

namespace Aurore::Protocol {
	namespace Configuration = Packets::Configuration;

	ProtocolSession::ProtocolSession(ServerStatus server_status)
		: m_ServerStatus(std::move(server_status)) {}

	ProtocolHandleResult ProtocolSession::HandlePacket(const PacketFrame& frame) {
		switch (m_State) {
		case ProtocolState::Handshake: return HandleHandshakeFrame(frame);
		case ProtocolState::Status: return HandleStatusFrame(frame);
		case ProtocolState::Login: return HandleLoginFrame(frame);
		case ProtocolState::Configuration: return HandleConfigurationFrame(frame);

		case ProtocolState::Play:
			return std::unexpected(ProtocolError::UnsupportedState);

		case ProtocolState::Disconnected:
			return std::unexpected(ProtocolError::UnexpectedPacket);
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

	ProtocolState ProtocolSession::GetState() const noexcept {
		return m_State;
	}

	bool ProtocolSession::IsDisconnected() const noexcept {
		return m_State == ProtocolState::Disconnected;
	}

	bool ProtocolSession::HasHandshake() const noexcept {
		return m_Handshake.has_value();
	}

	const HandshakeData* ProtocolSession::GetHandshake() const noexcept {
		if (!m_Handshake) return nullptr;
		return &*m_Handshake;
	}

	const ServerStatus& ProtocolSession::GetServerStatus() const noexcept {
		return m_ServerStatus;
	}

	const std::optional<ProtocolRequestId>& ProtocolSession::GetPendingRequestId() const noexcept {
		return m_PendingRequestId;
	}

	const std::optional<Packets::Configuration::Serverbound::ClientInformation>& ProtocolSession::GetClientInformation() const noexcept {
		return m_ClientInformation;
	}

	const std::optional<std::string>& ProtocolSession::GetClientBrand() const noexcept {
		return m_ClientBrand;
	}

	Aurore::Util::RegistryGeneration ProtocolSession::GetConfigurationGeneration() const noexcept {
		if (m_RegistrySnapshot)
			return m_RegistrySnapshot->GetGeneration();
		if (m_ConfigurationPlan)
			return m_ConfigurationPlan->GetGeneration();
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

	ProtocolHandleResult ProtocolSession::HandleHandshakePacket(
		const Packets::Handshake::Serverbound::Handshake& packet) {

		ProtocolAction action;

		switch (packet.Intention) {
		case HandshakeIntention::Status:
			/*
				Status remains available to mismatched clients so they can read the
				advertised server version and protocol number.
			*/
			m_Handshake = packet;
			m_State = ProtocolState::Status;
			return action;

		case HandshakeIntention::Login:
			m_Handshake = packet;

			if (packet.ProtocolVersion != m_ServerStatus.ProtocolVersion) {
				action.OutboundPackets.emplace_back(
					Packets::Login::Clientbound::Disconnect{
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

	ProtocolHandleResult ProtocolSession::HandleStatusPacket(
		const Packets::Status::Serverbound::Request& packet) {
		(void)packet;

		if (m_StatusPhase != StatusPhase::AwaitingRequest)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Status::Clientbound::Response{ .Status = m_ServerStatus });

		/*
			Construct the complete output before committing the phase
			transition.
		*/
		m_StatusPhase = StatusPhase::AwaitingPing;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleStatusPacket(
		const Packets::Status::Serverbound::Ping& packet) {
		if (m_StatusPhase != StatusPhase::AwaitingPing)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(Packets::Status::Clientbound::Pong{ .Payload = packet.Payload });
		action.Disconnect = true;

		/*
			The output packet is already owned by the action before the
			session commits its terminal Status transition.
		*/
		m_StatusPhase = StatusPhase::Complete;
		m_State = ProtocolState::Disconnected;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleLoginPacket(
		const Packets::Login::Serverbound::Start& packet) {
		if (m_LoginPhase != LoginPhase::AwaitingStart)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		const ProtocolRequestId request_id = AllocateRequestId();
		ProtocolAction action;

		action.Requests.emplace_back(LoginStartRequest{
			.Id = request_id,
			.Username = packet.Username,
			.PresentedProfileId = packet.PresentedProfileId
		});
		action.RequireInboundBoundary = true;

		/*
			Commit the pending phase only after the complete Core request
			has been constructed.
		*/
		m_PendingRequestId = request_id;
		m_LoginPhase = LoginPhase::AwaitingCoreDecision;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleLoginPacket(
		const Packets::Login::Serverbound::Acknowledged& packet) {
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
		ProtocolAction action;

		/*
			Client Information is independent of known-pack negotiation.
			The newest complete value replaces the previous one.
		*/
		m_ClientInformation = packet;
		return action;
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::ClientBrand& packet) {
		if (m_ConfigurationPhase == ConfigurationPhase::Complete)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		/*
			Client brand is informational and independent of the linear
			Configuration negotiation. A later complete value replaces
			the previous value, matching Client Information behavior.
		*/
		m_ClientBrand = packet.Brand;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::CustomPayload& packet) {
		if (m_ConfigurationPhase == ConfigurationPhase::Complete)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		/*
			Unknown custom channels are structurally validated and bounded by the
			packet decoder. Aurore currently has no plugin-channel subsystem, so
			the payload is deliberately ignored without advancing Configuration.

			When plugin messaging is implemented, this is the point at which the
			payload should become a Core request rather than being dispatched
			directly from Protocol.
		*/
		(void)packet;
		return ProtocolAction{};
	}

	ProtocolHandleResult ProtocolSession::HandleConfigurationPacket(const Configuration::Serverbound::SelectKnownPacks& packet) {
		if (m_ConfigurationPhase != ConfigurationPhase::AwaitingKnownPackSelection)
			return std::unexpected(ProtocolError::UnexpectedPacket);

		if (!m_ConfigurationPlan)
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		const auto selection = Configuration::ValidateKnownPackSelection(m_ConfigurationPlan->GetOfferedKnownPacks(), packet.Packs);
		if (!selection)
			return std::unexpected(ProtocolError::InvalidKnownPackSelection);

		auto frames = m_ConfigurationPlan->ReleasePostNegotiationFrames();
		if (frames.empty())
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		ProtocolAction action;
		action.OutboundPackets.reserve(frames.size());
		for (auto& frame : frames) {
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(frame) });
		}

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

		/*
			Do not accept Play-State packets until Core has admitted the
			connection into the following lifecycle.
		*/
		action.RequireInboundBoundary = true;

		m_PendingRequestId = request_id;
		m_ConfigurationPhase = ConfigurationPhase::AwaitingPlayAdmission;
		return action;
	}

	ProtocolHandleResult ProtocolSession::ResolveLogin(const LoginAcceptedResolution& resolution) {
		if (!MatchesPendingLoginRequest(resolution.Id) || resolution.Username.empty() || resolution.ProfileId.IsNil())
			return std::unexpected(ProtocolError::InvalidRequestResolution);

		ProtocolAction action;
		action.OutboundPackets.emplace_back(
			Packets::Login::Clientbound::Success{
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
		action.OutboundPackets.emplace_back(
			Packets::Login::Clientbound::Disconnect{
				.ReasonJson = resolution.ReasonJson
		});
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
			else return ResolveConfigurationRejected(value);
		}, std::move(resolution));
	}

	ProtocolHandleResult ProtocolSession::ResolveConfigurationReady(ConfigurationReadyResolution&& resolution) {
		if (!MatchesPendingConfigurationRequest(resolution.Id, ConfigurationPhase::AwaitingBootstrap))
			return std::unexpected(ProtocolError::InvalidRequestResolution);

		if (!resolution.Plan.GetSnapshot() || resolution.Plan.GetGeneration() == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(ProtocolError::ConfigurationGenerationMismatch);

		const auto initial_frames = resolution.Plan.GetInitialFrames();
		const auto post_negotiation_frames = resolution.Plan.GetPostNegotiationFrames();

		if (initial_frames.size() < 2 || post_negotiation_frames.size() < 2
			|| initial_frames.front().PacketId != Configuration::Clientbound::FeatureFlagsPacketId
			|| initial_frames.back().PacketId != Configuration::Clientbound::SelectKnownPacksPacketId
			|| post_negotiation_frames[post_negotiation_frames.size() - 2].PacketId != Configuration::Clientbound::TagsPacketId
			|| post_negotiation_frames.back().PacketId != Configuration::Clientbound::FinishConfigurationPacketId)
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		for (std::size_t index{ 0 }; index + 2 < post_negotiation_frames.size(); index++) {
			if (post_negotiation_frames[index].PacketId == Configuration::Clientbound::RegistryDataPacketId) continue;
			return std::unexpected(ProtocolError::MissingConfigurationPlan);
		}

		auto frames = resolution.Plan.ReleaseInitialFrames();
		if (frames.empty())
			return std::unexpected(ProtocolError::MissingConfigurationPlan);

		ProtocolAction action;
		action.OutboundPackets.reserve(frames.size());

		for (auto& frame : frames) {
			action.OutboundPackets.emplace_back(PreparedPacketFrame{ .Frame = std::move(frame) });
		}

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

		m_RegistrySnapshot = m_ConfigurationPlan->GetSnapshot();
		if (m_RegistrySnapshot->GetGeneration() != m_ConfigurationPlan->GetGeneration()) {
			m_RegistrySnapshot.reset();
			return std::unexpected(ProtocolError::ConfigurationGenerationMismatch);
		}

		ProtocolAction action;

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

