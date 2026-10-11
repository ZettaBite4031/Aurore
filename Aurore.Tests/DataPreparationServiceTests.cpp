#include <Aurore/Data/DataPreparation.hpp>
#include <Aurore/Data/DataPreparationService.hpp>
#include <Aurore/Data/MinecraftSource.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#ifndef AURORE_PROCESS_TEST_HELPER_PATH
#error "Process helper path was not provided by CMake."
#endif

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::DataPreparationOptions;
		using Aurore::Data::DataPreparationRequest;
		using Aurore::Data::DataPreparationRuntimeOptions;
		using Aurore::Data::DataPreparationService;
		using Aurore::Data::LocalJarSource;
		using Aurore::Data::MinecraftSourceKind;
		using Aurore::Data::MinecraftVersion;
		using Aurore::Data::RawDataPreparationErrorCode;

		[[nodiscard]]
		std::filesystem::path HelperPath() {
			return std::filesystem::path{
				AURORE_PROCESS_TEST_HELPER_PATH
			};
		}

		class DataPreparationServiceTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				m_Root =
					std::filesystem::
						temp_directory_path()
					/ "aurore-data-preparation-service-tests";

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
			DataPreparationRequest MakeRequest(
				std::string_view jar_name =
					"server.jar",
				bool create_jar = true) {

				const auto jar =
					m_Root
					/ jar_name;

				if (create_jar) {
					std::ofstream stream{
						jar,
						std::ios::binary
					};

					EXPECT_TRUE(
						stream.is_open());

					stream
						<< "fake-server-jar";
				}

				auto request =
					DataPreparationRequest::
						Create(
							DataPreparationOptions{
								.Version =
									MinecraftVersion{
										.Id =
											"1.21.11",
										.ProtocolVersion =
											774,
									},
								.Source =
									LocalJarSource{
										.JarPath =
											jar,
									},
								.CacheRoot =
									m_Root
									/ "cache",
							});

				EXPECT_TRUE(
					request.has_value());

				return std::move(
					request.value());
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(
		DataPreparationServiceTests,
		PreparesRawMinecraftDataEndToEnd) {

		const auto result =
			DataPreparationService::
				PrepareRaw(
					MakeRequest(),
					DataPreparationRuntimeOptions{
						.JavaExecutable =
							HelperPath(),
					});

		ASSERT_TRUE(
			result.has_value());

		EXPECT_EQ(
			result->Version.Id,
			"1.21.11");

		EXPECT_EQ(
			result->Version.ProtocolVersion,
			774);

		EXPECT_EQ(
			result->SourceKind,
			MinecraftSourceKind::
				LocalJar);

		EXPECT_FALSE(
			result->OfficiallyVerified);

		EXPECT_FALSE(
			result->RequiredJavaMajorVersion
				.has_value());

		EXPECT_TRUE(
			result->SourceJarPath
				.is_absolute());

		EXPECT_EQ(
			result->SourceSha1.size(),
			40u);

		EXPECT_GT(
			result->SourceFileSize,
			0u);

		EXPECT_EQ(
			result->JavaMajorVersion,
			21u);

		EXPECT_EQ(
			result->JavaVersionText,
			"21.0.8");

		EXPECT_TRUE(
			std::filesystem::
				is_regular_file(
					result->ReportsRoot
					/ "registries.json"));

		EXPECT_TRUE(
			std::filesystem::
				is_directory(
					result->DataRoot
					/ "minecraft"));
	}

	TEST_F(
		DataPreparationServiceTests,
		ReportsSourceResolutionFailure) {

		const auto result =
			DataPreparationService::
				PrepareRaw(
					MakeRequest(
						"missing.jar",
						false),
					DataPreparationRuntimeOptions{
						.JavaExecutable =
							HelperPath(),
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			RawDataPreparationErrorCode::
				SourceResolutionFailed);

		EXPECT_FALSE(
			result.error()
				.Message.empty());
	}

	TEST_F(
		DataPreparationServiceTests,
		ReportsJavaResolutionFailure) {

		const auto result =
			DataPreparationService::
				PrepareRaw(
					MakeRequest(),
					DataPreparationRuntimeOptions{
						.JavaExecutable =
							m_Root
							/ "missing-java",
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			RawDataPreparationErrorCode::
				JavaResolutionFailed);

		EXPECT_FALSE(
			result.error()
				.Message.empty());
	}

	TEST_F(
		DataPreparationServiceTests,
		ReportsGeneratorFailure) {

		const auto result =
			DataPreparationService::
				PrepareRaw(
					MakeRequest(
						"generator-failure.jar"),
					DataPreparationRuntimeOptions{
						.JavaExecutable =
							HelperPath(),
					});

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			RawDataPreparationErrorCode::
				DataGenerationFailed);

		EXPECT_EQ(
			result.error().ExitCode,
			23);

		EXPECT_NE(
			result.error()
				.StandardError
				.find(
					"simulated generator failure"),
			std::string::npos);
	}
}
