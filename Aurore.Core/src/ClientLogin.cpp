#include <Aurore/Core/ClientLogin.hpp>

namespace Aurore::Core {
	std::expected<ClientIdentity, ClientLoginError> ClientLogin::ResolveOffline(const Protocol::LoginStartRequest& request) const {
		if (!IsUsernameValid(request.Username))
			return std::unexpected(ClientLoginError::InvalidUsername);
		return ClientIdentity{
			.Username = request.Username,
			.UniqueId = Aurore::Util::Uuid::FromOfflinePlayerName(request.Username),
			.Properties = {},
		};
	}

	bool ClientLogin::IsUsernameValid(std::string_view username) noexcept {
		if (username.empty() || username.size() > 16) return false;
		for (const auto character : username) {
			const auto byte = static_cast<unsigned char>(character);
			if (byte <= 32 || byte >= 127) return false;
		}
		return true;
	}

	std::string ClientLogin::BuildDisconnectReason(ClientLoginError error) {
		switch (error) {
		case ClientLoginError::InvalidUsername:
			return R"({"text":"Invalid username."})";
		}
		return R"({"text":"Login could not be completed."})";
	}

	std::string ClientLogin::BuildDisconnectReason(ClientError error) {
		switch (error) {
		case ClientError::DuplicateUsername:
		case ClientError::DuplicateUniqueId:
		case ClientError::IdentityAlreadyAssigned:
			return R"({"text":"A player with that identity is already connected."})";

		case ClientError::PlayerCapacityExceeded:
			return R"({"text":"The server is full."})";

		case ClientError::InvalidIdentity:
			return R"({"text":"Invalid player identity."})";

		default:
			return R"({"text":"Login could not be completed."})";
		}
	}


}
