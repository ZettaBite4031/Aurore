#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <cstdint>
#include <vector>

namespace Aurore::Protocol::Packets::Play::Clientbound {
	inline constexpr std::int32_t LevelChunkWithLightPacketId{ 0x2C };
}

namespace Aurore::Protocol {
	struct PlayChunkBlock final {
		std::uint8_t X{ 0 };
		std::int32_t Y{ 0 };
		std::uint8_t Z{ 0 };
		Aurore::Util::ResourceLocation Block;
	};

	struct PlayChunkColumn final {
		std::int32_t X{ 0 };
		std::int32_t Z{ 0 };
		std::int32_t MinimumY{ -64 };
		std::int32_t Height{ 384 };
		Aurore::Util::ResourceLocation AirBlock;
		Aurore::Util::ResourceLocation Biome;
		std::vector<PlayChunkBlock> Blocks;
	};

	[[nodiscard]] ProtocolResult<PacketFrame> EncodeLevelChunkWithLight(const PlayChunkColumn& chunk, const Aurore::Util::RegistrySnapshot& registries);
}
