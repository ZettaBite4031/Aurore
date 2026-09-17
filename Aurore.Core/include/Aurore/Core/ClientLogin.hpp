#pragma once

#include <Aurore/Core/ClientLifecycle.hpp>

#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace Aurore::Core {
	enum class ClientLoginError : std::uint8_t {
		InvalidUsername,
	};

	class ClientLogin final {
	public:
		[[nodiscard]] std::expected<ClientIdentity, ClientLoginError> ResolveOffline(const Protocol::LoginStartRequest& request) const;

		[[nodiscard]] static bool IsUsernameValid(std::string_view username) noexcept;

		[[nodiscard]] static std::string BuildDisconnectReason(ClientLoginError error);

		[[nodiscard]] static std::string BuildDisconnectReason(ClientError error);
	};
}
