#include "HttpClient.hpp"

#include <Aurore/Build/Config.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

static_assert(Aurore::Build::IsLinux, "Linux HTTP backend compiled for non-Linux target.");

namespace {
	using Aurore::Data::Detail::HttpError;
	using Aurore::Data::Detail::HttpErrorCode;
	using Aurore::Data::Detail::HttpTextRequest;
	using Aurore::Data::Detail::HttpTextResponse;
	using Aurore::Data::Detail::MaximumHttpUrlBytes;

	class CurlGlobal final {
	public:
		CurlGlobal() noexcept
			: m_Result(::curl_global_init(CURL_GLOBAL_DEFAULT)) {}

		~CurlGlobal() {
			if (m_Result == CURLE_OK) ::curl_global_cleanup();
		}

		[[nodiscard]] CURLcode GetResult() const noexcept { return m_Result; }

	private:
		CURLcode m_Result;
	};

	class UniqueCurl final {
	public:
		UniqueCurl() noexcept
			: m_Handle(::curl_easy_init()) {}

		~UniqueCurl() {
			if (m_Handle != nullptr)
				::curl_easy_cleanup(m_Handle);
		}

		UniqueCurl(const UniqueCurl&) = delete;
		UniqueCurl& operator=(const UniqueCurl&) = delete;

		[[nodiscard]] CURL* Get() const noexcept { return m_Handle; }

		[[nodiscard]] explicit operator bool() const noexcept {
			return m_Handle != nullptr;
		}

	private:
		CURL* m_Handle{ nullptr };
	};

	[[nodiscard]]
	HttpError MakeError(HttpErrorCode code, CURLcode native_code = CURLE_OK, std::uint32_t status_code = 0, std::size_t observed = 0, std::size_t limit = 0, std::string message = {}) {
		if (message.empty() && native_code != CURLE_OK)
			message = ::curl_easy_strerror(native_code);

		return HttpError{
			.Code = code,
			.StatusCode = status_code,
			.NativeCode = static_cast<std::uint64_t>(native_code),
			.ObservedBytes = observed,
			.LimitBytes = limit,
			.Message = std::move(message),
		};
	}

	struct WriteState final {
		std::string* Output{ nullptr };

		std::size_t Limit{ 0 };
		std::size_t ObservedBytes{ 0 };

		bool LimitExceeded{ false };
	};

	std::size_t WriteBody(char* data, std::size_t size, std::size_t count, void* user_data) {
		auto& state = *static_cast<WriteState*>(user_data);
		if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
			state.ObservedBytes = std::numeric_limits<std::size_t>::max();
			state.LimitExceeded = true;
			return 0;
		}

		const std::size_t bytes = size * count;
		if (bytes > std::numeric_limits<std::size_t>::max() - state.ObservedBytes)
			state.ObservedBytes = std::numeric_limits<std::size_t>::max();
		else state.ObservedBytes += bytes;

		const std::size_t remaining = state.Output->size() >= state.Limit ? 0 : state.Limit - state.Output->size();
		if (bytes > remaining) {
			state.LimitExceeded = true;
			return 0;
		}

		state.Output->append(data, bytes);
		return bytes;
	}

}

namespace Aurore::Data::Detail {
	std::expected<HttpTextResponse, HttpError> HttpClient::GetText(const HttpTextRequest& request) {
		if (request.Url.empty())
			return std::unexpected(MakeError(HttpErrorCode::EmptyUrl));

		if (request.Url.size() > MaximumHttpUrlBytes)
			return std::unexpected(MakeError(HttpErrorCode::UrlTooLong));

		if (!request.Url.starts_with("https://"))
			return std::unexpected(MakeError(HttpErrorCode::InsecureUrl));

		if (request.MaximumResponseBytes == 0)
			return std::unexpected(MakeError(HttpErrorCode::InvalidResponseLimit));

		static const CurlGlobal curl_global;
		if (curl_global.GetResult() != CURLE_OK)
			return std::unexpected(MakeError(HttpErrorCode::InitializationFailed, curl_global.GetResult()));

		UniqueCurl handle;
		if (!handle) 
			return std::unexpected(MakeError(HttpErrorCode::InitializationFailed));

		HttpTextResponse result;
		result.Body.reserve(std::min<std::size_t>(request.MaximumResponseBytes, 64 * 1024));
		WriteState write_state{
			.Output = &result.Body,
			.Limit = request.MaximumResponseBytes,
		};

		CURLcode option_error{ CURLE_OK };
		const auto set = [&](CURLoption option, auto value) {
				option_error = ::curl_easy_setopt(handle.Get(), option, value);
				return option_error == CURLE_OK;
		};

		if (!set(CURLOPT_URL, request.Url.c_str()) || !set(CURLOPT_FOLLOWLOCATION, 1L) ||
			!set(CURLOPT_MAXREDIRS, static_cast<long>(request.MaximumRedirects)) ||
			!set(CURLOPT_PROTOCOLS_STR, "https") || !set(CURLOPT_REDIR_PROTOCOLS_STR, "https") ||
			!set(CURLOPT_CONNECTTIMEOUT_MS, 10'000L) || !set(CURLOPT_TIMEOUT_MS, 30'000L) ||
			!set(CURLOPT_NOSIGNAL, 1L) || !set(CURLOPT_ACCEPT_ENCODING, "") ||
			!set(CURLOPT_USERAGENT, "Aurore/0.1") || !set(CURLOPT_WRITEFUNCTION, &WriteBody) ||
			!set(CURLOPT_WRITEDATA, &write_state))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, option_error));

		const CURLcode perform_result = ::curl_easy_perform(handle.Get());
		if (write_state.LimitExceeded)
			return std::unexpected(MakeError(HttpErrorCode::ResponseTooLarge, CURLE_OK, 0, write_state.ObservedBytes, write_state.Limit));

		if (perform_result != CURLE_OK)
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, perform_result));

		long status_code{ 0 };
		const CURLcode info_result = ::curl_easy_getinfo(handle.Get(), CURLINFO_RESPONSE_CODE, &status_code);
		if (info_result != CURLE_OK) 
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, info_result));

		if (status_code < 0 || status_code > std::numeric_limits<std::uint32_t>::max())
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, CURLE_OK, 0, 0, 0, "HTTP status code was outside the supported range."));

		result.StatusCode = static_cast<std::uint32_t>(status_code);
		if (result.StatusCode < 200 || result.StatusCode >= 300) 
			return std::unexpected(MakeError(HttpErrorCode::HttpStatusFailed, CURLE_OK, result.StatusCode));

		return result;
	}
}
