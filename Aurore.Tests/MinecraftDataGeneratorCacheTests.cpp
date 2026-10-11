#include "JavaRuntime.hpp"
#include "MinecraftDataGenerator.hpp"

#include <Aurore/Data/MinecraftSource.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#ifndef AURORE_PROCESS_TEST_HELPER_PATH
#error "Process helper path was not provided by CMake."
#endif

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::MinecraftSourceArtifact;
		using Aurore::Data::MinecraftSourceKind;
		using Aurore::Data::MinecraftVersion;
		using Aurore::Data::Detail::JavaRuntime;
		using Aurore::Data::Detail::JavaRuntimeSource;
		using Aurore::Data::Detail::MinecraftDataGenerator;

		inline constexpr std::string_view
			CacheTestSha1{
				"0123456789abcdef0123456789abcdef01234567"
			};

		[[nodiscard]]
		std::filesystem::path CacheHelperPath() {
			return std::filesystem::path{
				AURORE_PROCESS_TEST_HELPER_PATH
			};
		}

		class MinecraftDataGeneratorCacheTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				m_Root =
					std::filesystem::
						temp_directory_path()
					/ "aurore-data-generator-cache-tests";

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
			JavaRuntime MakeJava() {
				return JavaRuntime{
					.Executable =
						CacheHelperPath(),
					.Source =
						JavaRuntimeSource::
							ExplicitPath,
					.MajorVersion = 21,
					.VersionText = "21",
				};
			}

			[[nodiscard]]
			MinecraftSourceArtifact MakeSource(
				std::string_view jar_name) {

				const auto jar =
					m_Root
					/ jar_name;

				std::ofstream stream{
					jar,
					std::ios::binary
				};

				EXPECT_TRUE(
					stream.is_open());

				stream
					<< "fake-server-jar";

				stream.close();

				return MinecraftSourceArtifact{
					.RequestedVersion =
						MinecraftVersion{
							.Id = "1.21.11",
							.ProtocolVersion = 774,
						},
					.JarPath =
						std::filesystem::
							absolute(jar),
					.SourceKind =
						MinecraftSourceKind::
							LocalJar,
					.FileSize = 15,
					.Sha1 =
						std::string{
							CacheTestSha1
						},
					.RequiredJavaMajorVersion =
						std::nullopt,
					.OfficiallyVerified = false,
				};
			}

			[[nodiscard]]
			std::filesystem::path
			GenerationRoot() const {
				return std::filesystem::
					absolute(
						m_Root
						/ "staging")
					/ "minecraft"
					/ CacheTestSha1;
			}

			void WriteCompleteGeneration() {
				const auto root =
					GenerationRoot();

				std::error_code error;

				std::filesystem::
					create_directories(
						root / "reports",
						error);

				ASSERT_FALSE(error);

				error.clear();

				std::filesystem::
					create_directories(
						root
							/ "data"
							/ "minecraft",
						error);

				ASSERT_FALSE(error);

				{
					std::ofstream report{
						root
							/ "reports"
							/ "registries.json"
					};

					ASSERT_TRUE(
						report.is_open());

					report << "{}";
				}

				{
					std::ofstream marker{
						root
							/ "data"
							/ "minecraft"
							/ "cached.json"
					};

					ASSERT_TRUE(
						marker.is_open());

					marker << "{}";
				}
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(
		MinecraftDataGeneratorCacheTests,
		ReusesCompleteGenerationWithoutLaunchingGenerator) {

		WriteCompleteGeneration();

		const auto marker =
			GenerationRoot()
			/ "keep-me.txt";

		{
			std::ofstream stream{
				marker
			};

			ASSERT_TRUE(
				stream.is_open());

			stream << "preserve";
		}

		/*
			The process test helper deliberately fails when the JAR
			name is generator-failure.jar. A successful result proves
			the generator process was never launched.
		*/
		const auto result =
			MinecraftDataGenerator::
				Generate(
					MakeSource(
						"generator-failure.jar"),
					MakeJava(),
					m_Root
						/ "staging");

		ASSERT_TRUE(
			result.has_value());

		EXPECT_EQ(
			result->Root,
			GenerationRoot());

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

		EXPECT_TRUE(
			std::filesystem::
				is_regular_file(
					marker));
	}
}
