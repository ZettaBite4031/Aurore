#include "MojangMetadata.hpp"

#include "Sha1.hpp"

#include <sonnet/sonnet.hpp>

#include <concepts>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
	using Aurore::Data::MinecraftVersion;
	using Aurore::Data::Detail::HttpError;
	using Aurore::Data::Detail::HttpTextGetter;
	using Aurore::Data::Detail::HttpTextRequest;
	using Aurore::Data::Detail::MaximumMojangManifestBytes;
	using Aurore::Data::Detail::MaximumMojangVersionMetadataBytes;
	using Aurore::Data::Detail::MojangMetadataError;
	using Aurore::Data::Detail::MojangMetadataErrorCode;
	using Aurore::Data::Detail::MojangVersionManifestUrl;
	using Aurore::Data::Detail::OfficialMinecraftVersion;

	[[nodiscard]] MojangMetadataError MakeError(MojangMetadataErrorCode code, std::string field = {}, std::string expected = {}, std::string observed = {}, std::string detail = {}) {
		return MojangMetadataError{
			.Code = code,
			.Field = std::move(field),
			.Expected = std::move(expected),
			.Observed = std::move(observed),
			.Detail = std::move(detail),
		};
	}

	[[nodiscard]] MojangMetadataError MakeHttpError(MojangMetadataErrorCode code, const HttpError& error) {
		auto result = MakeError(code);
		result.HttpFailure = error;
		return result;
	}

	[[nodiscard]] bool IsSha1(std::string_view value) noexcept {
		if (value.size() != 40) return false;

		for (const char character : value) {
			const bool digit = character >= '0' && character <= '9';
			const bool lower = character >= 'a' && character <= 'f';
			const bool upper = character >= 'A' && character <= 'F';
			if (!digit && !lower && !upper) return false;
		}
		return true;
	}

	[[nodiscard]] bool IsOfficialMetadataUrl(std::string_view url) noexcept {
		return url.starts_with("https://piston-meta.mojang.com/");
	}

	[[nodiscard]] bool IsOfficialServerUrl(std::string_view url) noexcept {
		return url.starts_with("https://piston-data.mojang.com/") || url.starts_with("https://launcher.mojang.com/");
	}

	[[nodiscard]] const Sonnet::value* FindMember(const Sonnet::value& value, std::string_view key) noexcept {
		if (!value.is_object()) return nullptr;
		return value.find(key);
	}

	[[nodiscard]] std::optional<std::string> ReadString(const Sonnet::value& value, std::string_view key) {
		const auto* member = FindMember(value, key);
		if (member == nullptr || !member->is_string()) return std::nullopt;
		return std::string{ member->as_string() };
	}

	template<typename T> requires std::integral<T> && (!std::same_as<T, bool>)
		[[nodiscard]] std::optional<T> ReadPositiveInteger(const Sonnet::value& value, std::string_view key) {
		const auto* member = FindMember(value, key);
		if (member == nullptr || !member->is_number()) return std::nullopt;
		const double number = member->as_number();
		if (!std::isfinite(number) || number <= 0.0 || std::trunc(number) != number) return std::nullopt;
		constexpr double MaximumExactJsonInteger{ 9'007'199'254'740'991.0 };
		if (number > MaximumExactJsonInteger) return std::nullopt;
		if constexpr (std::is_signed_v<T>) if (static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<T>::max())) return std::nullopt;
		else if (static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<T>::max())) return std::nullopt;
		return static_cast<T>(number);
	}

	[[nodiscard]] std::string HashText(std::string_view text) {
		Aurore::Data::Detail::Sha1 hasher;
		hasher.Update(std::as_bytes(std::span{ text.data(), text.size() }));
		return Aurore::Data::Detail::ToHex(hasher.Finalize());
	}

	struct ManifestVersionRecord final {
		std::string MetadataUrl;
		std::string MetadataSha1;
	};

	[[nodiscard]] std::expected<ManifestVersionRecord, MojangMetadataError> ResolveManifestRecord(std::string_view body, std::string_view requested_version) {
		auto parsed = Sonnet::parse(body);
		if (!parsed)
			return std::unexpected(MakeError(MojangMetadataErrorCode::ManifestParseFailed, {}, {}, {}, parsed.error().msg));
		if (!parsed->is_object())
			return std::unexpected(MakeError(MojangMetadataErrorCode::ManifestInvalid, "root"));

		const auto* versions = FindMember(*parsed, "versions");
		if (versions == nullptr || !versions->is_array())
			return std::unexpected(MakeError(MojangMetadataErrorCode::ManifestInvalid, "versions"));

		const Sonnet::value* match{ nullptr };
		for (const auto& entry : versions->as_array()) {
			const auto id = ReadString(entry, "id");
			if (!id || *id != requested_version) continue;
			if (match != nullptr)
				return std::unexpected(MakeError(MojangMetadataErrorCode::DuplicateVersion, "versions.id", std::string{ requested_version }, *id));
			match = &entry;
		}

		if (match == nullptr)
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionNotFound, "versions.id", std::string{ requested_version }));

		if (!match->is_object())
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionRecordInvalid));

		const auto metadata_url = ReadString(*match, "url");
		if (!metadata_url || !IsOfficialMetadataUrl(*metadata_url))
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataUrlInvalid, "versions.url", "https://pison-meta.mojang.com/...", metadata_url.value_or(std::string{})));

		const auto metadata_sha1 = ReadString(*match, "sha1");
		if (!metadata_sha1 || !IsSha1(*metadata_sha1))
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataSha1Invalid, "versions.sha1", "40 hexadecimal characters", metadata_sha1.value_or(std::string{})));

		return ManifestVersionRecord{
			.MetadataUrl = *metadata_url,
			.MetadataSha1 = *metadata_sha1,
		};
	}

	[[nodiscard]] std::expected<OfficialMinecraftVersion, MojangMetadataError> ParseVersionMetadata(MinecraftVersion version, const ManifestVersionRecord& manifest, std::string_view body) {
		const std::string observed_sha1 = HashText(body);
		if (observed_sha1 != manifest.MetadataSha1)
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataIntegrityMismatch, "version metadata sha1", manifest.MetadataSha1, observed_sha1));

		auto parsed = Sonnet::parse(body);
		if (!parsed)
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataParseFailed, {}, {}, {}, parsed.error().msg));
		if (!parsed->is_object())
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataInvalid, "root"));

		const auto id = ReadString(*parsed, "id");
		if (!id)
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionMetadataInvalid, "id"));
		if (*id != version.Id)
			return std::unexpected(MakeError(MojangMetadataErrorCode::VersionIdentityMismatch, "id", version.Id, *id));

		const auto* downloads = FindMember(*parsed, "downloads");
		const auto* server = downloads == nullptr ? nullptr : FindMember(*downloads, "server");
		if (server == nullptr || !server->is_object())
			return std::unexpected(MakeError(MojangMetadataErrorCode::ServerDownloadMissing, "downloads.server"));

		const auto server_url = ReadString(*server, "url");
		const auto server_sha1 = ReadString(*server, "sha1");
		const auto server_size = ReadPositiveInteger<std::uintmax_t>(*server, "size");
		if (!server_url || !server_sha1 || !server_size || !IsSha1(*server_sha1))
			return std::unexpected(MakeError(MojangMetadataErrorCode::ServerDownloadInvalid, "downloads.server"));
		if (!IsOfficialServerUrl(*server_url))
			return std::unexpected(MakeError(MojangMetadataErrorCode::ServerUrlInvalid, "downloads.server.url", "official Mojang HTTPS artifact host", *server_url));

		const auto* java_version = FindMember(*parsed, "javaVersion");
		if (java_version == nullptr || !java_version->is_object())
			return std::unexpected(MakeError(MojangMetadataErrorCode::JavaVersionMissing, "javaVersion"));

		const auto java_major = ReadPositiveInteger<std::uint32_t>(*java_version, "majorVersion");
		if (!java_major)
			return std::unexpected(MakeError(MojangMetadataErrorCode::JavaVersionInvalid, "javaVersion.majorVersion"));

		return OfficialMinecraftVersion{
			.Version = std::move(version),

			.MetadataUrl = manifest.MetadataUrl,
			.MetadataSha1 = manifest.MetadataSha1,

			.ServerUrl = *server_url,
			.ServerSha1 = *server_sha1,
			.ServerSize = *server_size,

			.RequiredJavaMajorVersion = *java_major,
		};
	}
}

namespace Aurore::Data::Detail {
	std::expected<OfficialMinecraftVersion, MojangMetadataError> MojangMetadataResolver::Resolve(MinecraftVersion version) {
		return Resolve(std::move(version), [](const HttpTextRequest& request) { return HttpClient::GetText(request); });
	}

	std::expected<OfficialMinecraftVersion, MojangMetadataError> MojangMetadataResolver::Resolve(MinecraftVersion version, const HttpTextGetter& get_text) {
		if (version.Id.empty())
			return std::unexpected(MakeError(MojangMetadataErrorCode::EmptyMinecraftVersion));

		const auto manifest_response = get_text(HttpTextRequest{ .Url = std::string{ MojangVersionManifestUrl }, .MaximumResponseBytes = MaximumMojangManifestBytes, .MaximumRedirects = 5 });
		if (!manifest_response)
			return std::unexpected(MakeHttpError(MojangMetadataErrorCode::ManifestRequestFailed, manifest_response.error()));

		auto manifest = ResolveManifestRecord(manifest_response->Body, version.Id);
		if (!manifest)
			return std::unexpected(manifest.error());

		const auto metadata_response = get_text(HttpTextRequest{ .Url = manifest->MetadataUrl, .MaximumResponseBytes = MaximumMojangVersionMetadataBytes, .MaximumRedirects = 5 });
		if (!metadata_response)
			return std::unexpected(MakeHttpError(MojangMetadataErrorCode::VersionMetadataRequestFailed, metadata_response.error()));

		return ParseVersionMetadata(std::move(version), *manifest, metadata_response->Body);
	}
}
