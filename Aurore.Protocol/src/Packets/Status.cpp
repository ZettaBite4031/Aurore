#include <Aurore/Protocol/Packets/Status.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Aurore::Protocol::Packets::Status {
	namespace {
		void AppendJsonEscaped(std::string& output, std::string_view value) {
			static constexpr char HexDigits[]{ "0123456789ABCDEF" };
			for (const auto character : value) {
				const auto byte = static_cast<unsigned char>(character);
				switch (byte) {
				case '"': output += "\\\""; break;
				case '\\': output += "\\\\"; break;
				case '\b': output += "\\b"; break;
				case '\f': output += "\\f"; break;
				case '\n': output += "\\n"; break;
				case '\r': output += "\\r"; break;
				case '\t': output += "\\t"; break;
				default:
					if (byte < 0x20) {
						output += "\\u00";
						output.push_back(HexDigits[(byte >> 4) & 0x0F]);
						output.push_back(HexDigits[byte & 0x0F]);
					}
					else output.push_back(character);
					break;
				}
			}
		}

		std::string BuildStatusResponseJson(const ServerStatus& status) {
			std::string result;
			result.reserve(status.VersionName.size() + status.Description.size() + 128);

			result += R"({"version":{"name":")";
			AppendJsonEscaped(result, status.VersionName);

			result += R"(","protocol":)";
			result += std::to_string(status.ProtocolVersion);

			result += R"(},"players":{"max":)";
			result += std::to_string(status.MaximumPlayers);

			result += R"(,"online":)";
			result += std::to_string(status.OnlinePlayers);

			result += R"(},"description":{"text":")";
			AppendJsonEscaped(result, status.Description);

			result += R"("}})";

			return result;
		}

		PacketFrame MakeFrame(std::int32_t packet_id, const Aurore::Util::ByteBuffer& payload) {
			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(payload.Bytes().begin(), payload.Bytes().end())
			};
		}
	}

	namespace Serverbound {
		ProtocolResult<Packet> Decode(const PacketFrame& frame) {
			switch (frame.PacketId) {
			case RequestPacketId:
				if (!frame.Payload.empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ std::in_place_type<Request> };
			case PingPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);

				const auto payload = reader.ReadInt64();
				if (!payload)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);
				return Packet{ Ping{.Payload = *payload} };
			} break;
			default: return std::unexpected(ProtocolError::UnexpectedPacket);
			}
		}
	}

	namespace Clientbound {
		PacketFrame Encode(const Response& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteString(BuildStatusResponseJson(packet.Status));
			return MakeFrame(ResponsePacketId, payload);
		}

		PacketFrame Encode(const Pong& packet) {
			Aurore::Util::ByteBuffer payload;
			payload.WriteInt64(packet.Payload);
			return MakeFrame(PongPacketId, payload);
		}

		PacketFrame Encode(const Packet& packet) {
			return std::visit([](const auto& value) { return Encode(value); }, packet);
		}
	}
}
