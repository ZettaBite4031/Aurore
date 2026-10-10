#include <Aurore/Data/DataPreparation.hpp>
#include <Aurore/Data/MinecraftSource.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::DataPreparationOptions;
		using Aurore::Data::DataPreparationRequest;
		using Aurore::Data::LocalJarSource;
		using Aurore::Data::MinecraftSourceErrorCode;
		using Aurore::Data::MinecraftSourceKind;
		using Aurore::Data::MinecraftSourceResolver;
		using Aurore::Data::MinecraftVersion;
		using Aurore::Data::OfficialDownloadSource;

		class MinecraftSourceResolverTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				m_Root =
					std::filesystem::
					temp_directory_path()
					/ "aurore-minecraft-source-tests";

				std::error_code error;

				std::filesystem::remove_all(
					m_Root,
					error);

				error.clear();

				ASSERT_TRUE(
					std::filesystem::
					create_directories(
						m_Root,
						error));

				ASSERT_FALSE(error);
			}

			void TearDown() override {
				std::error_code error;

				std::filesystem::remove_all(
					m_Root,
					error);
			}

			[[nodiscard]]
			DataPreparationRequest MakeLocalRequest(
				const std::filesystem::path& path) {

				auto request =
					DataPreparationRequest::Create(
						DataPreparationOptions{
							.Version =
								MinecraftVersion{
									.Id = "1.21.11",
									.ProtocolVersion = 774,
								},
							.Source =
								LocalJarSource{
									.JarPath = path,
								},
							.CacheRoot =
								m_Root / "cache",
						});

				EXPECT_TRUE(request.has_value());

				return std::move(
					request.value());
			}

			void WriteFile(
				const std::filesystem::path& path,
				std::string_view contents) {

				std::ofstream stream(
					path,
					std::ios::binary);

				ASSERT_TRUE(stream.is_open());

				stream.write(
					contents.data(),
					static_cast<std::streamsize>(
						contents.size()));

				ASSERT_TRUE(stream.good());
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(
		MinecraftSourceResolverTests,
		RejectsMissingLocalJar) {

		const auto path =
			m_Root / "missing.jar";

		const auto request =
			MakeLocalRequest(path);

		const auto result =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftSourceErrorCode::
			FileNotFound);
	}

	TEST_F(
		MinecraftSourceResolverTests,
		RejectsDirectoryAsLocalJar) {

		const auto path =
			m_Root / "server.jar";

		ASSERT_TRUE(
			std::filesystem::
			create_directory(path));

		const auto request =
			MakeLocalRequest(path);

		const auto result =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftSourceErrorCode::
			NotRegularFile);
	}

	TEST_F(
		MinecraftSourceResolverTests,
		RejectsEmptyLocalJar) {

		const auto path =
			m_Root / "server.jar";

		WriteFile(path, {});

		const auto request =
			MakeLocalRequest(path);

		const auto result =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftSourceErrorCode::
			EmptyFile);
	}

	TEST_F(
		MinecraftSourceResolverTests,
		ResolvesLocalJarArtifact) {

		const auto path =
			m_Root / "server.jar";

		constexpr std::string_view contents{
			"aurore-test-data"
		};

		WriteFile(path, contents);

		const auto request =
			MakeLocalRequest(path);

		const auto result =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->RequestedVersion.Id,
			"1.21.11");

		EXPECT_EQ(
			result->RequestedVersion.ProtocolVersion,
			774);

		EXPECT_EQ(
			result->SourceKind,
			MinecraftSourceKind::LocalJar);

		EXPECT_TRUE(
			result->JarPath.is_absolute());

		EXPECT_EQ(
			result->FileSize,
			contents.size());

		EXPECT_EQ(
			result->Sha1.size(),
			40u);
	}

	TEST_F(
		MinecraftSourceResolverTests,
		ComputesKnownSha1Digest) {

		const auto path =
			m_Root / "server.jar";

		/*
			Standard SHA-1 validation vector. Its length also exercises
			the extra padding block at finalization.
		*/
		constexpr std::string_view contents{
			"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
		};

		WriteFile(path, contents);

		const auto request =
			MakeLocalRequest(path);

		const auto result =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->Sha1,
			"84983e441c3bd26ebaae4aa1f95129e5e54670f1");
	}

	TEST_F(
		MinecraftSourceResolverTests,
		ProducesDeterministicIdentity) {

		const auto path =
			m_Root / "server.jar";

		WriteFile(
			path,
			"identical-source-bytes");

		const auto request =
			MakeLocalRequest(path);

		const auto first =
			MinecraftSourceResolver::Resolve(
				request);

		const auto second =
			MinecraftSourceResolver::Resolve(
				request);

		ASSERT_TRUE(first.has_value());
		ASSERT_TRUE(second.has_value());

		EXPECT_EQ(
			first->FileSize,
			second->FileSize);

		EXPECT_EQ(
			first->Sha1,
			second->Sha1);
	}

	TEST_F(
		MinecraftSourceResolverTests,
		LeavesOfficialDownloadForLaterMilestone) {

		const auto request =
			DataPreparationRequest::Create(
				DataPreparationOptions{
					.Version =
						MinecraftVersion{
							.Id = "1.21.11",
							.ProtocolVersion = 774,
						},
					.Source =
						OfficialDownloadSource{},
					.CacheRoot =
						m_Root / "cache",
				});

		ASSERT_TRUE(request.has_value());

		const auto result =
			MinecraftSourceResolver::Resolve(
				*request);

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftSourceErrorCode::
			SourceNotImplemented);
	}
}
