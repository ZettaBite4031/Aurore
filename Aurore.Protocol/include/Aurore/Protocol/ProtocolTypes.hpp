#pragma once

#include <Aurore/Util/UUID.hpp>

#include <compare>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Aurore::Protocol {
	enum class ProtocolState : std::uint8_t {
		Handshake,
		Status,
		Login,
		Configuration,
		Play,
		Disconnected,
	};

	enum class ProtocolError : std::uint8_t {
		UnexpectedPacket,
		MalformedPacket,
		InvalidNextState,
		TrailingPacketData,
		UnsupportedState,
		InvalidRequestResolution,
		InvalidKnownPackSelection,
		MissingConfigurationPlan,
		ConfigurationGenerationMismatch,
	};

	template<typename T>
	using ProtocolResult = std::expected<T, ProtocolError>;

	struct ServerStatus final {
		std::string VersionName{ "Aurore" };
		std::int32_t ProtocolVersion{ 774 };
		std::int32_t MaximumPlayers{ 20 };
		std::int32_t OnlinePlayers{ 0 };
		std::string Description{ "Aurore Testing Server" };
	};

	struct ProfileProperty final {
		std::string Name;
		std::string Value;
		std::optional<std::string> Signature;

		auto operator<=>(const ProfileProperty&) const = default;
	};

	struct ProtocolRequestId final {
		std::uint64_t Value{ 0 };

		[[nodiscard]] explicit operator bool() const noexcept {
			return Value != 0;
		}

		auto operator<=>(const ProtocolRequestId&) const noexcept = default;
	};

	struct LoginStartRequest final {
		ProtocolRequestId Id;

		std::string Username;

		/*
			This value is supplied by the remote client and is not yet an
			authenticated or authoritative identity.
		*/
		Aurore::Util::Uuid PresentedProfileId;
	};

	struct LoginAcceptedResolution final {
		ProtocolRequestId Id;

		Aurore::Util::Uuid ProfileId;
		std::string Username;

		std::vector<ProfileProperty> Properties;
	};

	struct LoginRejectedResolution final {
		ProtocolRequestId Id;
		std::string ReasonJson;
	};

	using LoginResolution = std::variant<LoginAcceptedResolution, LoginRejectedResolution>;

}
