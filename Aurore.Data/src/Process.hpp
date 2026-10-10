#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace Aurore::Data::Detail {
	inline constexpr std::size_t DefaultProcessCaptureLimit{ 8 * 1024 * 1024 };

	struct ProcessRequest final {
		std::filesystem::path Executable;
		std::vector<std::string> Arguments;

		std::size_t MaximumStandardOutputBytes{ DefaultProcessCaptureLimit };
		std::size_t MaximumStandardErrorBytes{ DefaultProcessCaptureLimit };
	};

	struct ProcessResult final {
		int ExitCode{ 0 };

		/*
			Present only on platforms where a process may terminate
			through a signal rather than a normal exit status.
		*/
		std::optional<int> TerminationSignal;

		std::string StdOut;
		std::string StdErr;
	};

	enum class ProcessErrorCode : std::uint8_t {
		PlatformNotSupported,
		EmptyExecutable,
		ArgumentEncodingFailed,

		PipeCreationFailed,
		StandardHandleCreationFailed,
		ProcessCreationFailed,
		ReaderCreationFailed,

		OutputReadFailed,
		StandardOutputLimitExceeded,
		StandardErrorLimitExceeded,

		WaitFailed,
		ExitCodeUnavailable,
	};

	struct ProcessError final {
		ProcessErrorCode Code;
		std::error_code SystemError;

		std::size_t ObservedBytes{ 0 };
		std::size_t LimitBytes{ 0 };
	};

	class Process final {
	public:
		[[nodiscard]] static std::expected<ProcessResult, ProcessError> Run(const ProcessRequest& request);
	};
}
