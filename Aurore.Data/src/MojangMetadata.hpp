#pragma once

#include "HttpClient.hpp"

#include <Aurore/Data/DataPreparation.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Aurore::Data::Detail {
	inline constexpr std::string_view MojangVersionManifestUrl{ "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json" };
	inline constexpr std::size_t MaximumMojangManifestBytes{ 4 * 1024 * 1024 };
	inline constexpr std::size_t MaximumMojangVersionMetadataBytes{ 4 * 1024 * 1024 };

	struct OfficialMinecraftVersion final {
		MinecraftVersion Version;

		std::string MetadataUrl;
		std::string MetadataSha1;

		std::string ServerUrl;
		std::string ServerSha1;
		std::uintmax_t ServerSize{ 0 };

		std::uint32_t RequiredJavaMajorVersion{ 0 };
	};

	enum class MojangMetadataErrorCode : std::uint8_t {
		EmptyMinecraftVersion,

		ManifestRequestFailed,
		ManifestParseFailed,
		ManifestInvalid,

		VersionNotFound,
		DuplicateVersion,
		VersionRecordInvalid,
		VersionMetadataUrlInvalid,
		VersionMetadataSha1Invalid,

		VersionMetadataRequestFailed,
		VersionMetadataIntegrityMismatch,
		VersionMetadataParseFailed,
		VersionMetadataInvalid,
		VersionIdentityMismatch,

		ServerDownloadMissing,
		ServerDownloadInvalid,
		ServerUrlInvalid,

		JavaVersionMissing,
		JavaVersionInvalid,
	};

	struct MojangMetadataError final {
		MojangMetadataErrorCode Code;

		std::optional<HttpError> HttpFailure;

		std::string Field;
		std::string Expected;
		std::string Observed;
		std::string Detail;
	};

	using HttpTextGetter = std::function<std::expected<HttpTextResponse, HttpError>(const HttpTextRequest&)>;

	class MojangMetadataResolver final {
	public:
		[[nodiscard]] static std::expected<OfficialMinecraftVersion, MojangMetadataError> Resolve(MinecraftVersion version);

		/*
			The injected transport exists so Mojang metadata policy
			can be tested without external network access.
		*/
		[[nodiscard]] static std::expected<OfficialMinecraftVersion, MojangMetadataError> Resolve(MinecraftVersion version, const HttpTextGetter& get_text);
	};
}
