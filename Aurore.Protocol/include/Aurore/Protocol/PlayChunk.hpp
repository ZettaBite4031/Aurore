#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aurore::Protocol::Packets::Play::Clientbound {
	inline constexpr std::int32_t LevelChunkWithLightPacketId{ 0x2C };
}

namespace Aurore::Protocol {
	enum class PlayHeightmapType : std::int32_t {
		WorldSurface = 1,
		MotionBlocking = 4,
	};

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

	struct ResolvedBlockStateDiagnostic final {
		Aurore::Util::ResourceLocation Block;
		std::uint32_t StateId{ 0 };

		bool operator==(const ResolvedBlockStateDiagnostic&) const noexcept = default;
	};

	struct PlayChunkSectionDiagnostic final {
		std::size_t Index{ 0 };
		std::int32_t MinimumY{ 0 };
		std::int32_t MaximumY{ 0 };
		std::int16_t NonAirBlocks{ 0 };
		std::uint8_t BitsPerBlock{ 0 };
		std::vector<std::uint32_t> Palette;

		bool operator==(const PlayChunkSectionDiagnostic&) const noexcept = default;
	};

	struct PlayHeightmapDiagnostic final {
		PlayHeightmapType Type{ PlayHeightmapType::WorldSurface };
		std::uint8_t BitsPerEntry{ 0 };
		std::vector<std::uint32_t> Values;
		std::vector<std::int64_t> PackedLongs;

		bool operator==(const PlayHeightmapDiagnostic&) const noexcept = default;
	};

	struct PlayLightDiagnostic final {
		std::size_t LightSectionCount{ 0 };
		std::uint64_t SkyLightMask{ 0 };
		std::uint64_t BlockLightMask{ 0 };
		std::uint64_t EmptySkyLightMask{ 0 };
		std::uint64_t EmptyBlockLightMask{ 0 };
		std::size_t SkyLightArrayCount{ 0 };
		std::size_t BlockLightArrayCount{ 0 };
		std::size_t BytesPerLightArray{ 0 };
	};

	struct PlayChunkEncodingDiagnostics final {
		std::uint32_t AirStateId{ 0 };
		Aurore::Util::RegistryRuntimeId BiomeRuntimeId{ 0 };
		std::size_t TotalNonAirBlocks{ 0 };
		std::vector<ResolvedBlockStateDiagnostic> ResolvedBlockStates;
		std::vector<PlayChunkSectionDiagnostic> Sections;
		std::vector<PlayHeightmapDiagnostic> Heightmaps;
		PlayLightDiagnostic Light;
	};

	[[nodiscard]] ProtocolResult<PacketFrame> EncodeLevelChunkWithLight(const PlayChunkColumn& chunk, const Aurore::Util::RegistrySnapshot& registries, PlayChunkEncodingDiagnostics* diagnostics = nullptr);
}
