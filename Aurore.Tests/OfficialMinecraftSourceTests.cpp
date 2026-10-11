#include "OfficialMinecraftSource.hpp"
#include "Sha1.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::
			MinecraftSourceKind;

		using Aurore::Data::
			MinecraftVersion;

		using Aurore::Data::Detail::
			HttpError;

		using Aurore::Data::Detail::
			HttpFileRequest;

		using Aurore::Data::Detail::
			HttpFileResponse;

		using Aurore::Data::Detail::
			OfficialMinecraftSourceAcquirer;

		using Aurore::Data::Detail::
			OfficialMinecraftSourceErrorCode;

		using Aurore::Data::Detail::
			OfficialMinecraftVersion;

		using Aurore::Data::Detail::
			Sha1;

		[[nodiscard]]
		std::string Hash(
			std::string_view value) {

			Sha1 hasher;

			hasher.Update(
				std::as_bytes(
					std::span{
						value.data(),
						value.size()
					}));

			return Aurore::Data::Detail::
				ToHex(
					hasher.Finalize());
		}

		[[nodiscard]]
		OfficialMinecraftVersion Metadata(
			std::string_view content) {

			return OfficialMinecraftVersion{
				.Version =
					MinecraftVersion{
						.Id = "1.21.11",
						.ProtocolVersion = 774,
					},
				.MetadataUrl =
					"https://piston-meta.mojang.com/test.json",
				.MetadataSha1 =
					"0123456789abcdef0123456789abcdef01234567",
				.ServerUrl =
					"https://piston-data.mojang.com/v1/objects/"
					+ Hash(content)
					+ "/server.jar",
				.ServerSha1 =
					Hash(content),
				.ServerSize =
					static_cast<std::uintmax_t>(
						content.size()),
				.RequiredJavaMajorVersion = 21,
			};
		}

		class OfficialMinecraftSourceTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				m_Root =
					std::filesystem::
						temp_directory_path()
					/ "aurore-official-source-tests";

				std::error_code error;

				std::filesystem::remove_all(
					m_Root,
					error);

				error.clear();

				std::filesystem::
					create_directories(
						m_Root,
						error);

				ASSERT_FALSE(error);
			}

			void TearDown() override {
				std::error_code error;

				std::filesystem::remove_all(
					m_Root,
					error);
			}

			[[nodiscard]]
			std::filesystem::path
			ExpectedArtifactPath(
				const OfficialMinecraftVersion& metadata) const {

				return std::filesystem::
					absolute(m_Root)
					/ "downloads"
					/ "minecraft"
					/ metadata.ServerSha1
					/ "server.jar";
			}

			static void Write(
				const std::filesystem::path& path,
				std::string_view content) {

				std::filesystem::
					create_directories(
						path.parent_path());

				std::ofstream stream{
					path,
					std::ios::binary
						| std::ios::trunc
				};

				ASSERT_TRUE(
					stream.is_open());

				stream.write(
					content.data(),
					static_cast<std::streamsize>(
						content.size()));

				ASSERT_TRUE(stream);
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		DownloadsAndPublishesVerifiedArtifact) {

		constexpr std::string_view content{
			"fake-official-server"
		};

		const auto metadata =
			Metadata(content);

		std::size_t downloads{ 0 };

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest& request)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						downloads++;

						EXPECT_EQ(
							request.Url,
							metadata.ServerUrl);

						EXPECT_EQ(
							request.MaximumResponseBytes,
							metadata.ServerSize);

						EXPECT_EQ(
							request.Destination
								.filename(),
							"server.jar.part");

						Write(
							request.Destination,
							content);

						return HttpFileResponse{
							.StatusCode = 200,
							.BytesWritten =
								metadata.ServerSize,
						};
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_EQ(
			downloads,
			1u);

		EXPECT_EQ(
			result->SourceKind,
			MinecraftSourceKind::
				OfficialDownload);

		EXPECT_TRUE(
			result->OfficiallyVerified);

		ASSERT_TRUE(
			result->RequiredJavaMajorVersion
				.has_value());

		EXPECT_EQ(
			*result->RequiredJavaMajorVersion,
			21u);

		EXPECT_EQ(
			result->Sha1,
			metadata.ServerSha1);

		EXPECT_EQ(
			result->FileSize,
			metadata.ServerSize);

		EXPECT_EQ(
			result->JarPath,
			ExpectedArtifactPath(
				metadata));

		EXPECT_TRUE(
			std::filesystem::
				is_regular_file(
					result->JarPath));

		EXPECT_FALSE(
			std::filesystem::exists(
				result->JarPath.string()
					+ ".part"));
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		ReusesValidCachedArtifactWithoutDownload) {

		constexpr std::string_view content{
			"fake-official-server"
		};

		const auto metadata =
			Metadata(content);

		Write(
			ExpectedArtifactPath(
				metadata),
			content);

		bool downloader_called{ false };

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest&)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						downloader_called = true;

						return std::unexpected(
							HttpError{});
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_FALSE(
			downloader_called);

		EXPECT_EQ(
			result->JarPath,
			ExpectedArtifactPath(
				metadata));
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		ReplacesCorruptedCachedArtifact) {

		constexpr std::string_view content{
			"fake-official-server"
		};

		const auto metadata =
			Metadata(content);

		Write(
			ExpectedArtifactPath(
				metadata),
			"corrupt-cache");

		std::size_t downloads{ 0 };

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest& request)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						downloads++;

						Write(
							request.Destination,
							content);

						return HttpFileResponse{
							.StatusCode = 200,
							.BytesWritten =
								metadata.ServerSize,
						};
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_EQ(
			downloads,
			1u);

		std::ifstream stream{
			result->JarPath,
			std::ios::binary
		};

		ASSERT_TRUE(
			stream.is_open());

		const std::string observed{
			std::istreambuf_iterator<char>{
				stream
			},
			std::istreambuf_iterator<char>{}
		};

		EXPECT_EQ(
			observed,
			content);
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		RejectsDownloadSizeMismatchAndDoesNotPublish) {

		constexpr std::string_view expected_content{
			"fake-official-server"
		};

		const auto metadata =
			Metadata(
				expected_content);

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest& request)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						Write(
							request.Destination,
							"short");

						return HttpFileResponse{
							.StatusCode = 200,
							.BytesWritten = 5,
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			OfficialMinecraftSourceErrorCode::
				DownloadedArtifactSizeMismatch);

		EXPECT_EQ(
			result.error().ExpectedSize,
			metadata.ServerSize);

		EXPECT_EQ(
			result.error().ObservedSize,
			5u);

		EXPECT_FALSE(
			std::filesystem::exists(
				ExpectedArtifactPath(
					metadata)));

		EXPECT_FALSE(
			std::filesystem::exists(
				ExpectedArtifactPath(
					metadata)
						.string()
					+ ".part"));
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		RejectsDownloadSha1MismatchAndDoesNotPublish) {

		constexpr std::string_view expected_content{
			"fake-official-server"
		};

		constexpr std::string_view wrong_content{
			"wrong-official-data!"
		};

		static_assert(
			expected_content.size()
				== wrong_content.size());

		const auto metadata =
			Metadata(
				expected_content);

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest& request)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						Write(
							request.Destination,
							wrong_content);

						return HttpFileResponse{
							.StatusCode = 200,
							.BytesWritten =
								metadata.ServerSize,
						};
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			OfficialMinecraftSourceErrorCode::
				DownloadedArtifactSha1Mismatch);

		EXPECT_EQ(
			result.error().ExpectedSha1,
			metadata.ServerSha1);

		EXPECT_EQ(
			result.error().ObservedSha1,
			Hash(wrong_content));

		EXPECT_FALSE(
			std::filesystem::exists(
				ExpectedArtifactPath(
					metadata)));

		EXPECT_FALSE(
			std::filesystem::exists(
				ExpectedArtifactPath(
					metadata)
						.string()
					+ ".part"));
	}

	TEST_F(
		OfficialMinecraftSourceTests,
		ReplacesStalePartialDownload) {

		constexpr std::string_view content{
			"fake-official-server"
		};

		const auto metadata =
			Metadata(content);

		const auto partial =
			ExpectedArtifactPath(
				metadata)
					.string()
			+ ".part";

		Write(
			partial,
			"stale");

		const auto result =
			OfficialMinecraftSourceAcquirer::
				Acquire(
					metadata,
					m_Root,
					[&](
						const HttpFileRequest& request)
						-> std::expected<
							HttpFileResponse,
							HttpError> {

						EXPECT_EQ(
							request.Destination,
							std::filesystem::path{
								partial
							});

						EXPECT_FALSE(
							std::filesystem::
								exists(
									request.Destination));

						Write(
							request.Destination,
							content);

						return HttpFileResponse{
							.StatusCode = 200,
							.BytesWritten =
								metadata.ServerSize,
						};
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_FALSE(
			std::filesystem::exists(
				partial));
	}
}
