#pragma once

#include "Process.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Aurore::Data::Detail {
	enum class JavaRuntimeSource : std::uint8_t {
		ExplicitPath,
		JavaHome,
		Path,
	};

	struct JavaVersion final {
		std::uint32_t Major{ 0 };
		std::string Text;
	};

	struct JavaRuntime final {
		std::filesystem::path Executable;

		JavaRuntimeSource Source;

		std::uint32_t MajorVersion{ 0 };
		std::string VersionText;
	};

	struct JavaRuntimeRequest final {
		std::optional<std::filesystem::path> ExplicitExecutable;
	};

	struct JavaRuntimeEnvironment final {
		std::optional<std::filesystem::path> JavaHome;
		std::vector<std::filesystem::path> SearchDirectories;
	};

	enum class JavaRuntimeErrorCode : std::uint8_t {
		NotFound,
		PathResolutionFailed,
		PathInspectionFailed,
		NotRegularFile,

		ProcessFailed,
		NonZeroExitCode,
		UnrecognizedVersion,
	};

	struct JavaRuntimeError final {
		JavaRuntimeErrorCode Code;

		std::filesystem::path Path;

		std::optional<ProcessError> ProcessFailure;

		int ExitCode{ 0 };

		std::string Output;
	};

	[[nodiscard]] std::optional<JavaVersion> ParseJavaVersion(std::string_view output);

	class JavaRuntimeResolver final {
	public:
		[[nodiscard]] static std::expected<JavaRuntime, JavaRuntimeError> Resolve(const JavaRuntimeRequest& request);

		/*
			Environment injection exists so discovery policy can be
			tested without mutating process-global environment state.
		*/
		[[nodiscard]] static std::expected<JavaRuntime, JavaRuntimeError> Resolve(const JavaRuntimeRequest& request, const JavaRuntimeEnvironment& environment);
	};
}
