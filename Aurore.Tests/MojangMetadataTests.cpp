#include "MojangMetadata.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::
			MinecraftVersion;

		using Aurore::Data::Detail::
			HttpError;

		using Aurore::Data::Detail::
			HttpErrorCode;

		using Aurore::Data::Detail::
			HttpTextRequest;

		using Aurore::Data::Detail::
			HttpTextResponse;

		using Aurore::Data::Detail::
			MaximumMojangManifestBytes;

		using Aurore::Data::Detail::
			MaximumMojangVersionMetadataBytes;

		using Aurore::Data::Detail::
			MojangMetadataErrorCode;

		using Aurore::Data::Detail::
			MojangMetadataResolver;

		using Aurore::Data::Detail::
			MojangVersionManifestUrl;

		inline constexpr std::string_view
			VersionMetadata{
R"({
  "id": "1.21.11",
  "downloads": {
    "server": {
      "sha1": "64bb6d763bed0a9f1d632ec347938594144943ed",
      "size": 56327581,
      "url": "https://piston-data.mojang.com/v1/objects/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar"
    }
  },
  "javaVersion": {
    "component": "java-runtime-delta",
    "majorVersion": 21
  }
})"
			};

		inline constexpr std::string_view
			VersionMetadataSha1{
				"8c50eb036cb9b36ecb532ddad7a191a82f019669"
			};

		[[nodiscard]]
		std::string Manifest(
			std::string_view version = "1.21.11",
			std::string_view sha1 =
				VersionMetadataSha1,
			std::string_view metadata_url = {}) {

			const std::string url =
				metadata_url.empty()
				? "https://piston-meta.mojang.com/v1/packages/"
					+ std::string{ sha1 }
					+ "/1.21.11.json"
				: std::string{
					metadata_url
				};

			return
				R"({"versions":[{"id":")"
				+ std::string{ version }
				+ R"(","sha1":")"
				+ std::string{ sha1 }
				+ R"(","url":")"
				+ url
				+ R"("}]})";
		}

		[[nodiscard]]
		MinecraftVersion TargetVersion() {
			return MinecraftVersion{
				.Id = "1.21.11",
				.ProtocolVersion = 774,
			};
		}
	}

	TEST(
		MojangMetadataTests,
		ResolvesOfficialVersionMetadata) {

		std::size_t request_count{ 0 };

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[&](
						const HttpTextRequest& request)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						request_count++;

						if (request_count == 1) {
							EXPECT_EQ(
								request.Url,
								MojangVersionManifestUrl);

							EXPECT_EQ(
								request.MaximumResponseBytes,
								MaximumMojangManifestBytes);

							return HttpTextResponse{
								.StatusCode = 200,
								.Body = Manifest(),
							};
						}

						EXPECT_EQ(
							request.Url,
							"https://piston-meta.mojang.com/v1/packages/8c50eb036cb9b36ecb532ddad7a191a82f019669/1.21.11.json");

						EXPECT_EQ(
							request.MaximumResponseBytes,
							MaximumMojangVersionMetadataBytes);

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								std::string{
									VersionMetadata
								},
						};
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_EQ(
			request_count,
			2u);

		EXPECT_EQ(
			result->Version.Id,
			"1.21.11");

		EXPECT_EQ(
			result->Version.ProtocolVersion,
			774);

		EXPECT_EQ(
			result->MetadataSha1,
			VersionMetadataSha1);

		EXPECT_EQ(
			result->ServerSha1,
			"64bb6d763bed0a9f1d632ec347938594144943ed");

		EXPECT_EQ(
			result->ServerSize,
			56'327'581u);

		EXPECT_EQ(
			result->ServerUrl,
			"https://piston-data.mojang.com/v1/objects/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar");

		EXPECT_EQ(
			result->RequiredJavaMajorVersion,
			21u);
	}

	TEST(
		MojangMetadataTests,
		RejectsUnknownMinecraftVersion) {

		const auto result =
			MojangMetadataResolver::
				Resolve(
					MinecraftVersion{
						.Id = "9.99.99",
						.ProtocolVersion = 774,
					},
					[](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						return HttpTextResponse{
							.StatusCode = 200,
							.Body = Manifest(),
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				VersionNotFound);
	}

	TEST(
		MojangMetadataTests,
		RejectsMalformedManifest) {

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								R"({"versions":{}})",
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				ManifestInvalid);
	}

	TEST(
		MojangMetadataTests,
		RejectsNonOfficialMetadataUrl) {

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								Manifest(
									"1.21.11",
									VersionMetadataSha1,
									"https://example.com/1.21.11.json"),
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				VersionMetadataUrlInvalid);
	}

	TEST(
		MojangMetadataTests,
		RejectsVersionMetadataIntegrityMismatch) {

		std::size_t request_count{ 0 };

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[&](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						request_count++;

						if (request_count == 1) {
							return HttpTextResponse{
								.StatusCode = 200,
								.Body =
									Manifest(
										"1.21.11",
										"0000000000000000000000000000000000000000"),
							};
						}

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								std::string{
									VersionMetadata
								},
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				VersionMetadataIntegrityMismatch);
	}

	TEST(
		MojangMetadataTests,
		RejectsNonOfficialServerUrl) {

		constexpr std::string_view
			metadata{
R"({"id":"1.21.11","downloads":{"server":{"sha1":"64bb6d763bed0a9f1d632ec347938594144943ed","size":56327581,"url":"https://example.com/server.jar"}},"javaVersion":{"majorVersion":21}})"
			};

		/*
			This fixture intentionally skips the manifest hash check by
			using a real hash calculated for the exact body below in
			the test setup.
		*/
		constexpr std::string_view
			metadata_sha1{
				"5a1ae90ac116c64364c92042eb4264ce4cb167b9"
			};

		std::size_t request_count{ 0 };

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[&](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						request_count++;

						if (request_count == 1) {
							return HttpTextResponse{
								.StatusCode = 200,
								.Body =
									Manifest(
										"1.21.11",
										metadata_sha1),
							};
						}

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								std::string{
									metadata
								},
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				ServerUrlInvalid);
	}

	TEST(
		MojangMetadataTests,
		RejectsMissingJavaRequirement) {

		constexpr std::string_view
			metadata{
R"({"id":"1.21.11","downloads":{"server":{"sha1":"64bb6d763bed0a9f1d632ec347938594144943ed","size":56327581,"url":"https://piston-data.mojang.com/v1/objects/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar"}}})"
			};

		constexpr std::string_view
			metadata_sha1{
				"89248894d3249b10d68524e3f0d9a3f198d6e83d"
			};

		std::size_t request_count{ 0 };

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[&](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						request_count++;

						if (request_count == 1) {
							return HttpTextResponse{
								.StatusCode = 200,
								.Body =
									Manifest(
										"1.21.11",
										metadata_sha1),
							};
						}

						return HttpTextResponse{
							.StatusCode = 200,
							.Body =
								std::string{
									metadata
								},
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				JavaVersionMissing);
	}

	TEST(
		MojangMetadataTests,
		PropagatesManifestHttpFailure) {

		const auto result =
			MojangMetadataResolver::
				Resolve(
					TargetVersion(),
					[](
						const HttpTextRequest&)
						-> std::expected<
							HttpTextResponse,
							HttpError> {

						return std::unexpected(
							HttpError{
								.Code =
									HttpErrorCode::
										RequestFailed,
								.NativeCode = 123,
								.Message =
									"simulated failure",
							});
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MojangMetadataErrorCode::
				ManifestRequestFailed);

		ASSERT_TRUE(
			result.error()
				.HttpFailure
				.has_value());

		EXPECT_EQ(
			result.error()
				.HttpFailure
				->NativeCode,
			123u);
	}
}
