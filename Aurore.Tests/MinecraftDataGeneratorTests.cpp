#include "JavaRuntime.hpp"
#include "MinecraftDataGenerator.hpp"

#include <Aurore/Data/MinecraftSource.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#ifndef AURORE_PROCESS_TEST_HELPER_PATH
#error "Process helper path was not provided by CMake."
#endif

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::
			MinecraftSourceArtifact;

		using Aurore::Data::
			MinecraftSourceKind;

		using Aurore::Data::
			MinecraftVersion;

		using Aurore::Data::Detail::
			JavaRuntime;

		using Aurore::Data::Detail::
			JavaRuntimeSource;

		using Aurore::Data::Detail::
			MinecraftDataGenerator;

		using Aurore::Data::Detail::
			MinecraftDataGeneratorErrorCode;

		inline constexpr std::string_view
			TestSha1{
				"0123456789abcdef0123456789abcdef01234567"
		};

		[[nodiscard]]
		std::filesystem::path
			HelperPath() {

			return std::filesystem::path{
				AURORE_PROCESS_TEST_HELPER_PATH
			};
		}

		class MinecraftDataGeneratorTests :
			public ::testing::Test {

		protected:
			void SetUp() override {
				m_Root =
					std::filesystem::
					temp_directory_path()
					/ "aurore-data-generator-tests";

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
			JavaRuntime MakeJava(
				std::uint32_t major = 21) {

				return JavaRuntime{
					.Executable =
						HelperPath(),
					.Source =
						JavaRuntimeSource::
							ExplicitPath,
					.MajorVersion =
						major,
					.VersionText =
						std::to_string(major),
				};
			}

			[[nodiscard]]
			MinecraftSourceArtifact MakeSource(
				std::string_view jar_name =
				"server.jar") {

				const auto jar =
					m_Root
					/ jar_name;

				std::ofstream stream{
					jar,
					std::ios::binary
				};

				EXPECT_TRUE(
					stream.is_open());

				stream << "fake-server-jar";
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
							TestSha1
						},
				};
			}

			[[nodiscard]]
			std::filesystem::path
				ExpectedGenerationRoot() const {

				return std::filesystem::
					absolute(
						m_Root
						/ "staging")
					/ "minecraft"
					/ TestSha1;
			}

			std::filesystem::path m_Root;
		};
	}
	TEST_F(
		MinecraftDataGeneratorTests,
		GeneratesValidatedRawMinecraftData) {

		const auto source =
			MakeSource();

		const auto java =
			MakeJava();

		const auto result =
			MinecraftDataGenerator::
			Generate(
				source,
				java,
				m_Root / "staging");

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->Version.Id,
			"1.21.11");

		EXPECT_EQ(
			result->Version.ProtocolVersion,
			774);

		EXPECT_EQ(
			result->SourceSha1,
			TestSha1);

		EXPECT_EQ(
			result->Root,
			ExpectedGenerationRoot());

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
		MinecraftDataGeneratorTests,
		RejectsIncompatibleJavaRuntime) {

		const auto result =
			MinecraftDataGenerator::
			Generate(
				MakeSource(),
				MakeJava(17),
				m_Root / "staging");

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftDataGeneratorErrorCode::
			IncompatibleJavaVersion);
	}

	TEST_F(
		MinecraftDataGeneratorTests,
		ReportsGeneratorFailure) {

		const auto result =
			MinecraftDataGenerator::
			Generate(
				MakeSource(
					"generator-failure.jar"),
				MakeJava(),
				m_Root / "staging");

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftDataGeneratorErrorCode::
			GeneratorFailed);

		EXPECT_EQ(
			result.error().ExitCode,
			23);

		EXPECT_NE(
			result.error().StdErr.find(
				"simulated generator failure"),
			std::string::npos);
	}

	TEST_F(
		MinecraftDataGeneratorTests,
		RejectsMissingRegistryReport) {

		const auto result =
			MinecraftDataGenerator::
			Generate(
				MakeSource(
					"missing-registry-report.jar"),
				MakeJava(),
				m_Root / "staging");

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftDataGeneratorErrorCode::
			RegistryReportMissing);
	}

	TEST_F(
		MinecraftDataGeneratorTests,
		RejectsMissingServerData) {

		const auto result =
			MinecraftDataGenerator::
			Generate(
				MakeSource(
					"missing-server-data.jar"),
				MakeJava(),
				m_Root / "staging");

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftDataGeneratorErrorCode::
			ServerDataMissing);
	}

	TEST_F(
		MinecraftDataGeneratorTests,
		ReplacesStaleGenerationTree) {

		const auto stale_file =
			ExpectedGenerationRoot()
			/ "stale.txt";

		std::filesystem::
			create_directories(
				stale_file.parent_path());

		{
			std::ofstream stream{
				stale_file
			};

			ASSERT_TRUE(
				stream.is_open());

			stream << "stale";
		}

		ASSERT_TRUE(
			std::filesystem::exists(
				stale_file));

		const auto result =
			MinecraftDataGenerator::
			Generate(
				MakeSource(),
				MakeJava(),
				m_Root / "staging");

		ASSERT_TRUE(result.has_value());

		EXPECT_FALSE(
			std::filesystem::exists(
				stale_file));

		EXPECT_TRUE(
			std::filesystem::
			is_regular_file(
				result->ReportsRoot
				/ "registries.json"));
	}

	TEST_F(
		MinecraftDataGeneratorTests,
		RejectsInvalidSourceIdentity) {

		auto source =
			MakeSource();

		source.Sha1 =
			"../../definitely-not-a-sha1";

		const auto result =
			MinecraftDataGenerator::
			Generate(
				source,
				MakeJava(),
				m_Root / "staging");

		ASSERT_FALSE(
			result.has_value());

		EXPECT_EQ(
			result.error().Code,
			MinecraftDataGeneratorErrorCode::
			InvalidSourceArtifact);
	}


}
