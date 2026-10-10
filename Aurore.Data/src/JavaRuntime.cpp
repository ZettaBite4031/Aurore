#include "JavaRuntime.hpp"

#include <Aurore/Build/Config.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

#if AURORE_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace {
	using Aurore::Data::Detail::JavaRuntime;
	using Aurore::Data::Detail::JavaRuntimeError;
	using Aurore::Data::Detail::JavaRuntimeErrorCode;
	using Aurore::Data::Detail::JavaRuntimeSource;
	using Aurore::Data::Detail::ParseJavaVersion;
	using Aurore::Data::Detail::Process;
	using Aurore::Data::Detail::ProcessRequest;

	[[nodiscard]] std::filesystem::path JavaExecutableName() {
		if constexpr (Aurore::Build::IsWindows) return "java.exe";
		return "java";
	}

	[[nodiscard]] std::vector<std::filesystem::path> SplitSearchPath(std::string_view path) {
		constexpr char separator = Aurore::Build::IsWindows ? ';' : ':';
		std::vector<std::filesystem::path> result;
		std::size_t begin{ 0 };
		while (begin <= path.size()) {
			const auto end = path.find(separator, begin);
			const auto part = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);

			/*
				Do not interpret an empty PATH component as the current
				directory. Java discovery should not implicitly execute
				a binary from Aurore's working directory 
			*/
			if (!part.empty())
				result.emplace_back(std::string{ part });

			if (end == std::string_view::npos) break;

			begin = end + 1;
		}

		return result;
	}

	[[nodiscard]] std::optional<std::filesystem::path> ReadJavaHome() {
#if AURORE_PLATFORM_WINDOWS
		const DWORD required = ::GetEnvironmentVariableW(L"JAVA_HOME", nullptr, 0);
		if (required == 0) return std::nullopt;

		std::wstring value(required, L'\0');
		const DWORD written = ::GetEnvironmentVariableW(L"JAVA_HOME", value.data(), required);
		if (written == 0 || written >= required) return std::nullopt;

		value.resize(written);
		return std::filesystem::path{ value };
#else
		const char* value = std::getenv("JAVA_HOME");
		if (value == nullptr || *value == '\0') return std::nullopt;
		return std::filesystem::path{ value };
#endif
	}

	[[nodiscard]] std::vector<std::filesystem::path> ReadSearchPath() {
#if AURORE_PLATFORM_WINDOWS
		const DWORD required = ::GetEnvironmentVariableW(L"PATH", nullptr, 0);
		if (required == 0) return {};

		std::wstring value(required, L'\0');
		const DWORD written = ::GetEnvironmentVariableW(L"PATH", value.data(), required);
		if (written == 0 || written >= required)
			return {};

		value.resize(written);

		std::vector<std::filesystem::path> result;
		std::size_t begin{ 0 };
		while (begin <= value.size()) {
			const auto end = value.find(L';', begin);
			const auto part = value.substr(begin, end == std::wstring::npos ? value.size() - begin : end - begin);
			if (!part.empty()) result.emplace_back(part);
			if (end == std::wstring::npos)
				break;

			begin = end + 1;
		}

		return result;
#else
		const char* value = std::getenv("PATH");
		if (value == nullptr) return {};
		return SplitSearchPath(value);
#endif
	}

	[[nodiscard]] Aurore::Data::Detail::JavaRuntimeEnvironment ReadEnvironment() {
		return Aurore::Data::Detail::JavaRuntimeEnvironment{
				.JavaHome = ReadJavaHome(),
				.SearchDirectories = ReadSearchPath(),
		};
	}

	[[nodiscard]] JavaRuntimeError MakeError(JavaRuntimeErrorCode code, std::filesystem::path path = {}) {
		return JavaRuntimeError{ .Code = code, .Path = std::move(path) };
	}

	[[nodiscard]] std::expected<JavaRuntime, JavaRuntimeError> ValidateCandidate(const std::filesystem::path& path, JavaRuntimeSource source) {
		std::error_code error;
		auto resolved = std::filesystem::absolute(path, error);
		if (error)
			return std::unexpected(MakeError(JavaRuntimeErrorCode::PathResolutionFailed, path));

		resolved = resolved.lexically_normal();
		const bool exists = std::filesystem::exists(resolved, error);
		if (error)
			return std::unexpected(MakeError(JavaRuntimeErrorCode::PathInspectionFailed, resolved));
		if (!exists)
			return std::unexpected(MakeError(JavaRuntimeErrorCode::NotFound, resolved));

		const bool regular = std::filesystem::is_regular_file(resolved, error);
		if (error)
			return std::unexpected(MakeError(JavaRuntimeErrorCode::PathInspectionFailed, resolved));
		if (!regular)
			return std::unexpected(MakeError(JavaRuntimeErrorCode::NotRegularFile, resolved));

		const auto process = Process::Run(ProcessRequest{ .Executable = resolved, .Arguments = { "-version" }, .MaximumStandardOutputBytes = 64 * 1024, .MaximumStandardErrorBytes = 64 * 1024 });
		if (!process) {
			auto result = MakeError(JavaRuntimeErrorCode::ProcessFailed, resolved);
			result.ProcessFailure = process.error();
			return std::unexpected(std::move(result));
		}

		if (process->ExitCode != 0) {
			auto result = MakeError(JavaRuntimeErrorCode::NonZeroExitCode, resolved);
			result.ExitCode = process->ExitCode;
			result.Output = process->StdOut + process->StdErr;
			return std::unexpected(std::move(result));
		}

		const std::string output = process->StdOut + process->StdErr;
		const auto version = ParseJavaVersion(output);
		if (!version) {
			auto result = MakeError(JavaRuntimeErrorCode::UnrecognizedVersion, resolved);
			result.Output = output;
			return std::unexpected(std::move(result));
		}

		return JavaRuntime{ .Executable = std::move(resolved), .Source = source, .MajorVersion = version->Major, .VersionText = version->Text };
	}
}

namespace Aurore::Data::Detail {
	std::optional<JavaVersion> ParseJavaVersion(std::string_view output) {
		constexpr std::string_view prefix{ "version \"" };
		const std::size_t prefix_position = output.find(prefix);
		if (prefix_position == std::string_view::npos) return std::nullopt;

		const std::size_t version_start = prefix_position + prefix.size();
		const std::size_t version_end = output.find('"', version_start);
		if (version_end == std::string::npos || version_end == version_start) return std::nullopt;

		const std::string_view version = output.substr(version_start, version_end - version_start);
		std::string_view major_text{ version };

		/*
			Java 8 and older use the historical 1.x version format.
			For example, 1.8.0_451 represents Java 8.
		*/
		if (version.starts_with("1."))
			major_text.remove_prefix(2);

		const std::size_t digit_end = major_text.find_first_not_of("0123456789");
		const std::string_view digits = digit_end == std::string_view::npos ? major_text : major_text.substr(0, digit_end);
		if (digits.empty()) return std::nullopt;

		std::uint32_t major{ 0 };
		const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), major);
		if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;

		return JavaVersion{ .Major = major, .Text = std::string{ version}, };
	}

	std::expected<JavaRuntime, JavaRuntimeError> JavaRuntimeResolver::Resolve(const JavaRuntimeRequest& request) {
		return Resolve(request, ReadEnvironment());
	}

	std::expected<JavaRuntime, JavaRuntimeError> JavaRuntimeResolver::Resolve(const JavaRuntimeRequest& request, const JavaRuntimeEnvironment& environment) {
		if (request.ExplicitExecutable)
			return ValidateCandidate(*request.ExplicitExecutable, JavaRuntimeSource::ExplicitPath);

		if (environment.JavaHome) {
			const auto candidate = *environment.JavaHome / "bin" / JavaExecutableName();

			/*
				JAVA_HOME is explicit environment configuration.
				If it is present but broken, report it rather than
				silently selecting an unrelated Java from PATH.
			*/
			return ValidateCandidate(candidate, JavaRuntimeSource::JavaHome);
		}

		for (const auto& directory : environment.SearchDirectories) {
			const auto candidate = directory / JavaExecutableName();
			std::error_code error;
			const bool exists = std::filesystem::exists(candidate, error);
			if (error || !exists) continue;

			/*
				Match normal PATH semantics: the first matching
				Java candidate is authoritative.
			*/
			return ValidateCandidate(candidate, JavaRuntimeSource::Path);
		}

		return std::unexpected(MakeError(JavaRuntimeErrorCode::NotFound));
	}
}
