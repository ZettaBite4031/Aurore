#include "Process.hpp"

#include <gtest/gtest.h>

#include <Aurore/Build/Config.hpp>

#include <filesystem>
#include <string>
#include <string_view>

#ifndef AURORE_PROCESS_TEST_HELPER_PATH
#error "Process helper path was not provided by CMake."
#endif

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::Detail::Process;
		using Aurore::Data::Detail::ProcessErrorCode;
		using Aurore::Data::Detail::ProcessRequest;

		[[nodiscard]]
		std::filesystem::path HelperPath() {
			return std::filesystem::path{
				AURORE_PROCESS_TEST_HELPER_PATH
			};
		}

		[[nodiscard]]
		constexpr std::string_view NativeLineEnding() noexcept {
			if constexpr (Aurore::Build::IsWindows) return "\r\n";
			return "\n";
		}
	}

	TEST(
		ProcessTests,
		RejectsEmptyExecutable) {

		const auto result =
			Process::Run(
				ProcessRequest{});

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			ProcessErrorCode::
			EmptyExecutable);
	}

	TEST(
		ProcessTests,
		CapturesOutputAndExitCode) {

		const auto result =
			Process::Run(
				ProcessRequest{
					.Executable =
						HelperPath(),
					.Arguments = {
						"capture",
					},
				});

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->ExitCode,
			7);

		EXPECT_FALSE(
			result->TerminationSignal
			.has_value());

		const std::string newline{ NativeLineEnding() };

		EXPECT_EQ(
			result->StdOut,
			std::string{ "stdout-text" } + newline
		);

		EXPECT_EQ(
			result->StdErr,
			std::string{ "stderr-text" } + newline
		);
	}

	TEST(
		ProcessTests,
		PreservesArgumentBoundaries) {

		const auto result =
			Process::Run(
				ProcessRequest{
					.Executable =
						HelperPath(),
					.Arguments = {
						"arguments",
						"hello world",
						R"(quoted"value)",
						R"(trailing\)",
					},
				});

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->ExitCode,
			0);

		const std::string newline{ NativeLineEnding() };

		std::string expected;

		expected += "hello world";
		expected += newline;

		expected += "quoted\"value";
		expected += newline;

		expected += "trailing\\";
		expected += newline;

		EXPECT_EQ(
			result->StdOut,
			expected
		);
	}

	TEST(
		ProcessTests,
		RejectsMissingExecutable) {

		const auto result =
			Process::Run(
				ProcessRequest{
					.Executable =
						HelperPath()
						.parent_path()
						/ "definitely-does-not-exist",
				});

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			ProcessErrorCode::
			ProcessCreationFailed);

		EXPECT_TRUE(
			static_cast<bool>(
				result.error()
				.SystemError));
	}

	TEST(
		ProcessTests,
		BoundsCapturedStandardOutput) {

		constexpr std::size_t limit{
			64
		};

		const auto result =
			Process::Run(
				ProcessRequest{
					.Executable =
						HelperPath(),
					.Arguments = {
						"stdout-flood",
					},
					.MaximumStandardOutputBytes =
						limit,
				});

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			ProcessErrorCode::
			StandardOutputLimitExceeded);

		EXPECT_GT(
			result.error()
			.ObservedBytes,
			limit);

		EXPECT_EQ(
			result.error()
			.LimitBytes,
			limit);
	}
}
