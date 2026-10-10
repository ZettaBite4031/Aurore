#include "JavaRuntime.hpp"

#include <gtest/gtest.h>

#include <Aurore/Build/Config.hpp>

#include <filesystem>
#include <system_error>

#ifndef AURORE_PROCESS_TEST_HELPER_PATH
#error "Process helper path was not provided by CMake."
#endif

namespace {
	void CopyExecutable(
		const std::filesystem::path& source,
		const std::filesystem::path&
		destination) {

		std::filesystem::create_directories(
			destination.parent_path());

		std::filesystem::copy_file(
			source,
			destination,
			std::filesystem::
			copy_options::overwrite_existing);

		std::error_code error;

		std::filesystem::permissions(
			destination,
			std::filesystem::perms::
			owner_exec
			| std::filesystem::perms::
			group_exec
			| std::filesystem::perms::
			others_exec,
			std::filesystem::
			perm_options::add,
			error);
	}
}

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::Detail::
			JavaRuntimeEnvironment;

		using Aurore::Data::Detail::
			JavaRuntimeErrorCode;

		using Aurore::Data::Detail::
			JavaRuntimeRequest;

		using Aurore::Data::Detail::
			JavaRuntimeResolver;

		using Aurore::Data::Detail::
			JavaRuntimeSource;

		using Aurore::Data::Detail::
			ParseJavaVersion;

		[[nodiscard]]
		std::filesystem::path
			HelperPath() {

			return std::filesystem::path{
				AURORE_PROCESS_TEST_HELPER_PATH
			};
		}

		[[nodiscard]]
		std::filesystem::path
			JavaExecutableName() {

			if constexpr (
				Aurore::Build::IsWindows) {

				return "java.exe";
			}

			return "java";
		}
	}

	TEST(
		JavaRuntimeTests,
		ParsesModernJavaVersion) {

		const auto version =
			ParseJavaVersion(
				"openjdk version "
				"\"21.0.8\" "
				"2026-07-21");

		ASSERT_TRUE(
			version.has_value());

		EXPECT_EQ(
			version->Major,
			21u);

		EXPECT_EQ(
			version->Text,
			"21.0.8");
	}

	TEST(
		JavaRuntimeTests,
		ParsesLegacyJavaVersion) {

		const auto version =
			ParseJavaVersion(
				"java version "
				"\"1.8.0_451\"");

		ASSERT_TRUE(
			version.has_value());

		EXPECT_EQ(
			version->Major,
			8u);

		EXPECT_EQ(
			version->Text,
			"1.8.0_451");
	}

	TEST(
		JavaRuntimeTests,
		RejectsUnrecognizedVersion) {

		EXPECT_FALSE(
			ParseJavaVersion(
				"definitely not java")
			.has_value());
	}

	TEST(
		JavaRuntimeTests,
		ValidatesExplicitJavaExecutable) {

		const auto result =
			JavaRuntimeResolver::Resolve(
				JavaRuntimeRequest{
					.ExplicitExecutable =
						HelperPath(),
				},
				JavaRuntimeEnvironment{});

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->Source,
			JavaRuntimeSource::
			ExplicitPath);

		EXPECT_EQ(
			result->MajorVersion,
			21u);

		EXPECT_EQ(
			result->VersionText,
			"21.0.8");

		EXPECT_TRUE(
			result->Executable
			.is_absolute());
	}

	TEST(
		JavaRuntimeTests,
		DiscoversJavaFromJavaHome) {

		const auto root =
			std::filesystem::temp_directory_path()
			/ "aurore-java-home-test";

		std::error_code error;
		std::filesystem::remove_all(
			root,
			error);

		const auto executable =
			root
			/ "bin"
			/ JavaExecutableName();

		CopyExecutable(
			HelperPath(),
			executable);

		const auto result =
			JavaRuntimeResolver::Resolve(
				JavaRuntimeRequest{},
				JavaRuntimeEnvironment{
					.JavaHome = root,
				});

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->Source,
			JavaRuntimeSource::
			JavaHome);

		EXPECT_EQ(
			result->MajorVersion,
			21u);

		std::filesystem::remove_all(
			root,
			error);
	}

	TEST(
		JavaRuntimeTests,
		DiscoversJavaFromSearchPath) {

		const auto root =
			std::filesystem::temp_directory_path()
			/ "aurore-java-path-test";

		std::error_code error;
		std::filesystem::remove_all(
			root,
			error);

		const auto executable =
			root
			/ JavaExecutableName();

		CopyExecutable(
			HelperPath(),
			executable);

		const auto result =
			JavaRuntimeResolver::Resolve(
				JavaRuntimeRequest{},
				JavaRuntimeEnvironment{
					.SearchDirectories = {
						root,
					},
				});

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->Source,
			JavaRuntimeSource::
			Path);

		EXPECT_EQ(
			result->MajorVersion,
			21u);

		std::filesystem::remove_all(
			root,
			error);
	}

	TEST(
		JavaRuntimeTests,
		BrokenExplicitPathDoesNotFallBack) {

		const auto result =
			JavaRuntimeResolver::Resolve(
				JavaRuntimeRequest{
					.ExplicitExecutable =
						"missing-java",
				},
				JavaRuntimeEnvironment{
					.SearchDirectories = {
						HelperPath()
							.parent_path(),
					},
				});

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			JavaRuntimeErrorCode::
			NotFound);
	}
}
