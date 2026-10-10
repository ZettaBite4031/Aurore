#include "Process.hpp"

namespace Aurore::Data::Detail {
	std::expected<ProcessResult, ProcessError> Process::Run(const ProcessRequest& request) {
		(void)request;
		return std::unexpected(ProcessError{ .Code = ProcessErrorCode::PlatformNotSupported, });
	}
}
