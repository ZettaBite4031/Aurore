#pragma once

#include "HttpClient.hpp"
#include "MojangMetadata.hpp"

#include <Aurore/Data/MinecraftSource.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace Aurore::Data::Detail {
	enum class OfficialMinecraftSourceErrorCode : std::uint8_t {
		InvalidMetadata,

		CachePathResolutionFailed,
		CacheInspectionFailed,
		CacheDirectoryCreationFailed,
		CacheCleanupFailed,

		CachedArtifactReadFailed,

		DownloadFailed,
		DownloadedArtifactReadFailed,
		DownloadedArtifactSizeMismatch,
		DownloadedArtifactSha1Mismatch,

		CachePublicationFailed,
	};

	struct OfficialMinecraftSourceError final {
		OfficialMinecraftSourceErrorCode Code;

		std::filesystem::path Path;
		std::error_code SystemError;

		std::optional<HttpError> HttpFailure;

		std::uintmax_t ExpectedSize{ 0 };
		std::uintmax_t ObservedSize{ 0 };

		std::string ExpectedSha1;
		std::string ObservedSha1;
	};

	using HttpFileDownloader = std::function<std::expected<HttpFileResponse, HttpError>(const HttpFileRequest&)>;

	class OfficialMinecraftSourceAcquirer final {
	public:
		[[nodiscard]] static std::expected<MinecraftSourceArtifact, OfficialMinecraftSourceError> Acquire(const OfficialMinecraftVersion& metadata, const std::filesystem::path& cache_root);

		/*
			The injected downloader keeps cache and verification tests
			fully offline while production uses HttpClient.
		*/
		[[nodiscard]] static std::expected<MinecraftSourceArtifact, OfficialMinecraftSourceError> Acquire(const OfficialMinecraftVersion& metadata, const std::filesystem::path& cache_root, const HttpFileDownloader& download);
	};
}
