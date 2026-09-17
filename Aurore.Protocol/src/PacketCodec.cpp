#include <Aurore/Protocol/PacketCodec.hpp>

#include <utility>

namespace {
	class ClientboundPacketEncoder final {
	public:
		Aurore::Protocol::PacketFrame operator()(const Aurore::Protocol::Packets::Status::Clientbound::Response& packet) const {
			return Aurore::Protocol::Packets::Status::Clientbound::Encode(packet);
		}

		Aurore::Protocol::PacketFrame operator()(const Aurore::Protocol::Packets::Status::Clientbound::Pong& packet) const {
			return Aurore::Protocol::Packets::Status::Clientbound::Encode(packet);
		}

		Aurore::Protocol::PacketFrame operator()(const Aurore::Protocol::Packets::Login::Clientbound::Disconnect& packet) const {
			return Aurore::Protocol::Packets::Login::Clientbound::Encode(packet);
		}

		Aurore::Protocol::PacketFrame operator()(const Aurore::Protocol::Packets::Login::Clientbound::Success& packet) const {
			return Aurore::Protocol::Packets::Login::Clientbound::Encode(packet);
		}

		Aurore::Protocol::PacketFrame operator()(Aurore::Protocol::PreparedPacketFrame&& packet) const noexcept {
			return std::move(packet.Frame);
		}
	};
}

namespace Aurore::Protocol {
	PacketFrame EncodeClientboundPacket(ClientboundPacket packet) {
		return std::visit(ClientboundPacketEncoder{}, std::move(packet));
	}
}

