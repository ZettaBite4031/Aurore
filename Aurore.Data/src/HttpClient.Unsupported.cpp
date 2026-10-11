#include "HttpClient.hpp"

namespace Aurore::Data::Detail {
	std::expected<
		HttpTextResponse,
		HttpError>
	HttpClient::GetText(
		const HttpTextRequest&) {

		return std::unexpected(
			HttpError{
				.Code =
					HttpErrorCode::
						PlatformNotSupported,
			});
	}

	std::expected<
		HttpFileResponse,
		HttpError>
	HttpClient::DownloadFile(
		const HttpFileRequest&) {

		return std::unexpected(
			HttpError{
				.Code =
					HttpErrorCode::
						PlatformNotSupported,
			});
	}
}
