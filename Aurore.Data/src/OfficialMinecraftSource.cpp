#include "OfficialMinecraftSource.hpp"

#include "FileIdentity.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace {
	using Aurore::Data::MinecraftSourceArtifact;
	using Aurore::Data::MinecraftSourceKind;
	using Aurore::Data::Detail::FileIdentity;
	using Aurore::Data::Detail::FileIdentityError;
	using Aurore::Data::Detail::HttpFileDownloader;
	using Aurore::Data::Detail::HttpFileRequest;
	using Aurore::Data::Detail::OfficialMinecraftSourceError;
	using Aurore::Data::Detail::OfficialMinecraftSourceErrorCode;
	using Aurore::Data::Detail::OfficialMinecraftVersion;

	[[nodiscard]] OfficialMinecraftSourceError MakeError(OfficialMinecraftSourceErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}) {
		return OfficialMinecraftSourceError{
			.Code = code,
			.Path = std::move(path),
			.SystemError = system_error,
		};
	}

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;
		return std::ranges::all_of(value, [](char character) {
				return std::isxdigit(static_cast<unsigned char>(character)) != 0;
		});
	}

	[[nodiscard]]
	bool IsOfficialServerUrl(std::string_view url) noexcept {
		return url.starts_with("https://piston-data.mojang.com/") || url.starts_with("https://launcher.mojang.com/");
	}

	[[nodiscard]] bool IdentityMatches(const FileIdentity& identity, const OfficialMinecraftVersion& metadata) {
		return identity.Size == metadata.ServerSize && identity.Sha1 == metadata.ServerSha1;
	}

	[[nodiscard]] MinecraftSourceArtifact MakeArtifact(const OfficialMinecraftVersion& metadata, std::filesystem::path path) {
		return MinecraftSourceArtifact{
			.RequestedVersion = metadata.Version,
			.JarPath = std::move(path),
			.SourceKind = MinecraftSourceKind::OfficialDownload,
			.FileSize = metadata.ServerSize,
			.Sha1 = metadata.ServerSha1,
			.RequiredJavaMajorVersion = metadata.RequiredJavaMajorVersion,
			.OfficiallyVerified = true,
		};
	}

	[[nodiscard]] OfficialMinecraftSourceError TranslateIdentityError(OfficialMinecraftSourceErrorCode code, const FileIdentityError& error) {
		return OfficialMinecraftSourceError{
			.Code = code,
			.Path = error.Path,
			.SystemError = error.SystemError,
		};
	}

	void RemoveBestEffort(const std::filesystem::path& path) noexcept {
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}
}

namespace Aurore::Data::Detail {
	std::expected<MinecraftSourceArtifact, OfficialMinecraftSourceError> OfficialMinecraftSourceAcquirer::Acquire(const OfficialMinecraftVersion& metadata, const std::filesystem::path& cache_root) {
		return Acquire(metadata, cache_root, [](const HttpFileRequest& request) { return HttpClient::DownloadFile(request); });
	}

	std::expected<MinecraftSourceArtifact, OfficialMinecraftSourceError> OfficialMinecraftSourceAcquirer::Acquire(const OfficialMinecraftVersion& metadata, const std::filesystem::path& cache_root, const HttpFileDownloader& download) {
		if (metadata.Version.Id.empty() || metadata.ServerSize == 0 || !IsSha1(metadata.ServerSha1) || metadata.RequiredJavaMajorVersion == 0 || !IsOfficialServerUrl(metadata.ServerUrl))
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::InvalidMetadata));

		if (cache_root.empty())
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CachePathResolutionFailed));

		std::error_code error;
		auto resolved_cache_root = std::filesystem::absolute(cache_root, error);
		if (error)
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CachePathResolutionFailed, cache_root, error));

		resolved_cache_root = resolved_cache_root.lexically_normal();
		const auto artifact_root = resolved_cache_root / "downloads" / "minecraft" / metadata.ServerSha1;
		const auto final_path = artifact_root / "server.jar";
		const auto partial_path = artifact_root / "server.jar.part";
		const bool final_exists = std::filesystem::exists(final_path, error);
		if (error)
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CacheInspectionFailed, final_path, error));

		if (final_exists) {
			const bool regular = std::filesystem::is_regular_file(final_path, error);
			if (error)
				return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CacheInspectionFailed, final_path, error));
			if (regular) {
				auto cached_identity = ReadFileIdentity(final_path);
				if (!cached_identity)
					return std::unexpected(TranslateIdentityError(OfficialMinecraftSourceErrorCode::CachedArtifactReadFailed, cached_identity.error()));
				if (IdentityMatches(*cached_identity, metadata))
					return MakeArtifact(metadata, final_path);
			}

			error.clear();
			std::filesystem::remove_all(final_path, error);
			if (error)
				return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CacheCleanupFailed, final_path, error));
		}

		error.clear();
		std::filesystem::create_directories(artifact_root, error);
		if (error)
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CacheDirectoryCreationFailed, artifact_root, error));

		error.clear();
		std::filesystem::remove_all(partial_path, error);
		if (error)
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CacheCleanupFailed, partial_path, error));

		const auto downloaded = download(HttpFileRequest{ .Url = metadata.ServerUrl, .Destination = partial_path, .MaximumResponseBytes = metadata.ServerSize, .MaximumRedirects = 5 });
		if (!downloaded) {
			RemoveBestEffort(partial_path);
			auto result = MakeError(OfficialMinecraftSourceErrorCode::DownloadFailed, partial_path);
			result.HttpFailure = downloaded.error();
			return std::unexpected(std::move(result));
		}

		auto identity = ReadFileIdentity(partial_path);
		if (!identity) {
			RemoveBestEffort(partial_path);
			return std::unexpected(TranslateIdentityError(OfficialMinecraftSourceErrorCode::DownloadedArtifactReadFailed, identity.error()));
		}

		if (identity->Size != metadata.ServerSize) {
			RemoveBestEffort(partial_path);
			auto result = MakeError(OfficialMinecraftSourceErrorCode::DownloadedArtifactSizeMismatch, partial_path);
			result.ExpectedSize = metadata.ServerSize;
			result.ObservedSize = identity->Size;
			return std::unexpected(std::move(result));
		}

		if (identity->Sha1 != metadata.ServerSha1) {
			RemoveBestEffort(partial_path);
			auto result = MakeError(OfficialMinecraftSourceErrorCode::DownloadedArtifactSha1Mismatch, partial_path);
			result.ExpectedSha1 = metadata.ServerSha1;
			result.ObservedSha1 = identity->Sha1;
			return std::unexpected(std::move(result));
		}

		error.clear();
		std::filesystem::rename(partial_path, final_path, error);
		if (error) {
			/*
				Another process may have published the same
				content-addressed artifact while this process was
				downloading. Accept that winner only if it verifies.
			*/
			std::error_code inspection_error;
			const bool raced_file_exists = std::filesystem::exists(final_path, inspection_error);
			if (!inspection_error && raced_file_exists && std::filesystem::is_regular_file(final_path, inspection_error) && !inspection_error) {
				auto raced_identity = ReadFileIdentity(final_path);
				if (raced_identity && IdentityMatches(*raced_identity, metadata)) {
					RemoveBestEffort(partial_path);
					return MakeArtifact(metadata, final_path);
				}
			}

			RemoveBestEffort(partial_path);
			return std::unexpected(MakeError(OfficialMinecraftSourceErrorCode::CachePublicationFailed, final_path, error));
		}

		return MakeArtifact(metadata, final_path);
	}
}
