#pragma once

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/PacketCodec.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>

#include <Aurore/Protocol/Packets/Handshake.hpp>
#include <Aurore/Protocol/Packets/Status.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Aurore::Protocol {
	using HandshakeIntention = Packets::Handshake::Intention;
	using HandshakeData = Packets::Handshake::Serverbound::Handshake;

	struct ConfigurationStartRequest final {
		ProtocolRequestId Id;
	};

	struct ConfigurationCompleteRequest final {
		ProtocolRequestId Id;
		Aurore::Util::RegistryGeneration Generation{ Aurore::Util::NoRegistryGeneration };
	};

	using ProtocolRequest = std::variant<
		LoginStartRequest,
		ConfigurationStartRequest,
		ConfigurationCompleteRequest>;

	struct ConfigurationReadyResolution final {
		ProtocolRequestId Id;
		ConfigurationTransmissionPlan Plan;
	};

	struct ConfigurationAcceptedResolution final {
		ProtocolRequestId Id;
	};

	struct ConfigurationRejectedResolution final {
		ProtocolRequestId Id;
	};

	using ConfigurationResolution = std::variant<
		ConfigurationReadyResolution,
		ConfigurationAcceptedResolution,
		ConfigurationRejectedResolution>;

	struct ProtocolAction final {
		std::vector<ClientboundPacket> OutboundPackets;
		std::vector<ProtocolRequest> Requests;

		/*
			When true, ProtocolConnection must stop at this Core decision
			boundary and reject any already-buffered later packet.
		*/
		bool RequireInboundBoundary{ false };
		bool Disconnect{ false };
	};

	using ProtocolHandleResult = std::expected<ProtocolAction, ProtocolError>;

	class ProtocolSession final {
	public:
		ProtocolSession() = default;

		explicit ProtocolSession(ServerStatus server_status);

		ProtocolSession(const ProtocolSession&) = delete;
		ProtocolSession& operator=(const ProtocolSession&) = delete;

		ProtocolSession(ProtocolSession&&) = default;
		ProtocolSession& operator=(ProtocolSession&&) = default;

		[[nodiscard]] ProtocolHandleResult HandlePacket(const PacketFrame& frame);
		[[nodiscard]] ProtocolHandleResult ResolveLogin(const LoginResolution& resolution);
		[[nodiscard]] ProtocolHandleResult ResolveConfiguration(ConfigurationResolution resolution);

		void Disconnect() noexcept;

		[[nodiscard]] ProtocolState GetState() const noexcept;
		[[nodiscard]] bool IsDisconnected() const noexcept;
		[[nodiscard]] bool HasHandshake() const noexcept;
		[[nodiscard]] const HandshakeData* GetHandshake() const noexcept;
		[[nodiscard]] const ServerStatus& GetServerStatus() const noexcept;
		[[nodiscard]] const std::optional<ProtocolRequestId>& GetPendingRequestId() const noexcept;
		[[nodiscard]] const std::optional<Packets::Configuration::Serverbound::ClientInformation>& GetClientInformation() const noexcept;
		[[nodiscard]] const std::optional<std::string>& GetClientBrand() const noexcept;
		[[nodiscard]] Aurore::Util::RegistryGeneration GetConfigurationGeneration() const noexcept;
		[[nodiscard]] const std::shared_ptr<const Aurore::Util::RegistrySnapshot>& GetRegistrySnapshot() const noexcept;

	private:
		enum class StatusPhase : std::uint8_t {
			AwaitingRequest,
			AwaitingPing,
			Complete
		};

		enum class LoginPhase : std::uint8_t {
			AwaitingStart,
			AwaitingCoreDecision,
			AwaitingAcknowledgement,
			Complete,
		};

		enum class ConfigurationPhase : std::uint8_t {
			AwaitingBootstrap,
			AwaitingKnownPackSelection,
			AwaitingFinishAcknowledgement,
			AwaitingPlayAdmission,
			Complete,
		};

		[[nodiscard]] ProtocolHandleResult HandleHandshakeFrame(const PacketFrame& frame);
		[[nodiscard]] ProtocolHandleResult HandleStatusFrame(const PacketFrame& frame);
		[[nodiscard]] ProtocolHandleResult HandleLoginFrame(const PacketFrame& frame);
		[[nodiscard]] ProtocolHandleResult HandleConfigurationFrame(const PacketFrame& frame);

		[[nodiscard]] ProtocolHandleResult HandleHandshakePacket(
			const Packets::Handshake::Serverbound::Handshake& packet);

		[[nodiscard]] ProtocolHandleResult HandleStatusPacket(
			const Packets::Status::Serverbound::Request& packet);

		[[nodiscard]] ProtocolHandleResult HandleStatusPacket(
			const Packets::Status::Serverbound::Ping& packet);

		[[nodiscard]] ProtocolHandleResult HandleLoginPacket(
			const Packets::Login::Serverbound::Start& packet);

		[[nodiscard]] ProtocolHandleResult HandleLoginPacket(
			const Packets::Login::Serverbound::Acknowledged& packet);

		[[nodiscard]] ProtocolHandleResult HandleConfigurationPacket(const Packets::Configuration::Serverbound::ClientInformation& packet);
		[[nodiscard]] ProtocolHandleResult HandleConfigurationPacket(const Packets::Configuration::Serverbound::ClientBrand& packet);
		[[nodiscard]] ProtocolHandleResult HandleConfigurationPacket(const Packets::Configuration::Serverbound::CustomPayload& packet);
		[[nodiscard]] ProtocolHandleResult HandleConfigurationPacket(const Packets::Configuration::Serverbound::SelectKnownPacks& packet);
		[[nodiscard]] ProtocolHandleResult HandleConfigurationPacket(const Packets::Configuration::Serverbound::FinishConfiguration& packet);

		[[nodiscard]] ProtocolHandleResult ResolveLogin(const LoginAcceptedResolution& resolution);
		[[nodiscard]] ProtocolHandleResult ResolveLogin(const LoginRejectedResolution& resolution);

		[[nodiscard]] ProtocolHandleResult ResolveConfigurationReady(ConfigurationReadyResolution&& resolution);
		[[nodiscard]] ProtocolHandleResult ResolveConfigurationAccepted(const ConfigurationAcceptedResolution& resolution);
		[[nodiscard]] ProtocolHandleResult ResolveConfigurationRejected(const ConfigurationRejectedResolution& resolution);

		[[nodiscard]] bool MatchesPendingLoginRequest(ProtocolRequestId id) const noexcept;
		[[nodiscard]] bool MatchesPendingConfigurationRequest(ProtocolRequestId id, ConfigurationPhase phase) const noexcept;

		[[nodiscard]] ProtocolRequestId AllocateRequestId() noexcept;


		ProtocolState m_State{ ProtocolState::Handshake };
		StatusPhase m_StatusPhase{ StatusPhase::AwaitingRequest };
		LoginPhase m_LoginPhase{ LoginPhase::AwaitingStart };
		ConfigurationPhase m_ConfigurationPhase{ ConfigurationPhase::AwaitingBootstrap };

		ServerStatus m_ServerStatus;
		std::optional<HandshakeData> m_Handshake;

		std::optional<ProtocolRequestId> m_PendingRequestId;
		std::uint64_t m_NextRequestValue{ 1 };

		std::optional<ConfigurationTransmissionPlan> m_ConfigurationPlan;
		std::optional<Packets::Configuration::Serverbound::ClientInformation> m_ClientInformation;
		std::optional<std::string> m_ClientBrand;
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> m_RegistrySnapshot;
	};
}
