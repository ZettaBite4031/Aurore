#include "HttpClient.hpp"

#include <Aurore/Build/Config.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

static_assert(
	Aurore::Build::IsLinux,
	"Linux HTTP backend compiled for non-Linux target.");

namespace {
	using Aurore::Data::Detail::HttpError;
	using Aurore::Data::Detail::HttpErrorCode;
	using Aurore::Data::Detail::HttpFileRequest;
	using Aurore::Data::Detail::HttpFileResponse;
	using Aurore::Data::Detail::HttpTextRequest;
	using Aurore::Data::Detail::HttpTextResponse;
	using Aurore::Data::Detail::MaximumHttpUrlBytes;

	class CurlGlobal final {
	public:
		CurlGlobal() noexcept
			: m_Result(
				::curl_global_init(
					CURL_GLOBAL_DEFAULT)) {}

		~CurlGlobal() {
			if (m_Result == CURLE_OK)
				::curl_global_cleanup();
		}

		[[nodiscard]]
		CURLcode GetResult() const noexcept {
			return m_Result;
		}

	private:
		CURLcode m_Result;
	};

	class UniqueCurl final {
	public:
		UniqueCurl() noexcept
			: m_Handle(
				::curl_easy_init()) {}

		~UniqueCurl() {
			if (m_Handle != nullptr)
				::curl_easy_cleanup(
					m_Handle);
		}

		UniqueCurl(
			const UniqueCurl&) = delete;

		UniqueCurl&
		operator=(
			const UniqueCurl&) = delete;

		[[nodiscard]]
		CURL* Get() const noexcept {
			return m_Handle;
		}

		[[nodiscard]]
		explicit operator bool() const noexcept {
			return m_Handle != nullptr;
		}

	private:
		CURL* m_Handle{ nullptr };
	};

	[[nodiscard]]
	HttpError MakeError(
		HttpErrorCode code,
		CURLcode native_code = CURLE_OK,
		std::uint32_t status_code = 0,
		std::uintmax_t observed = 0,
		std::uintmax_t limit = 0,
		std::string message = {}) {

		if (message.empty()
			&& native_code != CURLE_OK) {

			message =
				::curl_easy_strerror(
					native_code);
		}

		return HttpError{
			.Code = code,
			.StatusCode = status_code,
			.NativeCode =
				static_cast<std::uint64_t>(
					native_code),
			.ObservedBytes = observed,
			.LimitBytes = limit,
			.Message = std::move(message),
		};
	}

	[[nodiscard]]
	std::expected<void, HttpError>
	ValidateRequest(
		std::string_view url,
		std::uintmax_t maximum_response_bytes) {

		if (url.empty()) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						EmptyUrl));
		}

		if (url.size()
			> MaximumHttpUrlBytes) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						UrlTooLong));
		}

		if (!url.starts_with(
			"https://")) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						InsecureUrl));
		}

		if (maximum_response_bytes == 0) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						InvalidResponseLimit));
		}

		return {};
	}

	template<typename WriteFunction>
	[[nodiscard]]
	std::expected<
		UniqueCurl,
		HttpError>
	ConfigureRequest(
		const std::string& url,
		std::uint32_t maximum_redirects,
		WriteFunction write_function,
		void* write_data) {

		static const CurlGlobal
			curl_global;

		if (curl_global.GetResult()
			!= CURLE_OK) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						InitializationFailed,
					curl_global
						.GetResult()));
		}

		UniqueCurl handle;

		if (!handle) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						InitializationFailed));
		}

		CURLcode option_error{
			CURLE_OK
		};

		const auto set =
			[&](
				CURLoption option,
				auto value) {

				option_error =
					::curl_easy_setopt(
						handle.Get(),
						option,
						value);

				return option_error
					== CURLE_OK;
			};

		if (!set(
			CURLOPT_URL,
			url.c_str())
			|| !set(
				CURLOPT_FOLLOWLOCATION,
				1L)
			|| !set(
				CURLOPT_MAXREDIRS,
				static_cast<long>(
					maximum_redirects))
			|| !set(
				CURLOPT_PROTOCOLS_STR,
				"https")
			|| !set(
				CURLOPT_REDIR_PROTOCOLS_STR,
				"https")
			|| !set(
				CURLOPT_CONNECTTIMEOUT_MS,
				10'000L)
			|| !set(
				CURLOPT_TIMEOUT_MS,
				30'000L)
			|| !set(
				CURLOPT_NOSIGNAL,
				1L)
			|| !set(
				CURLOPT_ACCEPT_ENCODING,
				"")
			|| !set(
				CURLOPT_USERAGENT,
				"Aurore/0.1")
			|| !set(
				CURLOPT_WRITEFUNCTION,
				write_function)
			|| !set(
				CURLOPT_WRITEDATA,
				write_data)) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						RequestFailed,
					option_error));
		}

		return std::move(handle);
	}

	[[nodiscard]]
	std::expected<std::uint32_t, HttpError>
	ReadStatusCode(
		CURL* handle) {

		long status_code{ 0 };

		const CURLcode info_result =
			::curl_easy_getinfo(
				handle,
				CURLINFO_RESPONSE_CODE,
				&status_code);

		if (info_result != CURLE_OK) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						RequestFailed,
					info_result));
		}

		if (status_code < 0
			|| status_code
				> std::numeric_limits<
					std::uint32_t>::max()) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						RequestFailed,
					CURLE_OK,
					0,
					0,
					0,
					"HTTP status code was outside the supported range."));
		}

		const auto result =
			static_cast<std::uint32_t>(
				status_code);

		if (result < 200
			|| result >= 300) {

			return std::unexpected(
				MakeError(
					HttpErrorCode::
						HttpStatusFailed,
					CURLE_OK,
					result));
		}

		return result;
	}

	struct TextWriteState final {
		std::string* Output{ nullptr };

		std::uintmax_t Limit{ 0 };
		std::uintmax_t ObservedBytes{ 0 };

		bool LimitExceeded{ false };
	};

	std::size_t WriteText(
		char* data,
		std::size_t size,
		std::size_t count,
		void* user_data) {

		auto& state =
			*static_cast<
				TextWriteState*>(
					user_data);

		if (size != 0
			&& count
				> std::numeric_limits<
					std::size_t>::max()
					/ size) {

			state.ObservedBytes =
				std::numeric_limits<
					std::uintmax_t>::max();

			state.LimitExceeded = true;
			return 0;
		}

		const std::size_t bytes =
			size * count;

		const auto wide_bytes =
			static_cast<std::uintmax_t>(
				bytes);

		if (wide_bytes
			> state.Limit
				- state.ObservedBytes) {

			state.ObservedBytes +=
				wide_bytes;

			state.LimitExceeded = true;
			return 0;
		}

		state.ObservedBytes +=
			wide_bytes;

		state.Output->append(
			data,
			bytes);

		return bytes;
	}

	struct FileWriteState final {
		std::ofstream* Stream{ nullptr };

		std::uintmax_t Limit{ 0 };
		std::uintmax_t ObservedBytes{ 0 };

		bool LimitExceeded{ false };
		bool WriteFailed{ false };
	};

	std::size_t WriteFile(
		char* data,
		std::size_t size,
		std::size_t count,
		void* user_data) {

		auto& state =
			*static_cast<
				FileWriteState*>(
					user_data);

		if (size != 0
			&& count
				> std::numeric_limits<
					std::size_t>::max()
					/ size) {

			state.ObservedBytes =
				std::numeric_limits<
					std::uintmax_t>::max();

			state.LimitExceeded = true;
			return 0;
		}

		const std::size_t bytes =
			size * count;

		const auto wide_bytes =
			static_cast<std::uintmax_t>(
				bytes);

		if (wide_bytes
			> state.Limit
				- state.ObservedBytes) {

			state.ObservedBytes +=
				wide_bytes;

			state.LimitExceeded = true;
			return 0;
		}

		state.Stream->write(
			data,
			static_cast<std::streamsize>(
				bytes));

		if (!*state.Stream) {
			state.WriteFailed = true;
			return 0;
		}

		state.ObservedBytes +=
			wide_bytes;

		return bytes;
	}
}

namespace Aurore::Data::Detail {
	std::expected<
		HttpTextResponse,
		HttpError>
	HttpClient::GetText(
		const HttpTextRequest& request) {

		auto valid =
			ValidateRequest(
				request.Url,
				request.MaximumResponseBytes);

		if (!valid)
			return std::unexpected(
				valid.error());

		HttpTextResponse result;

		result.Body.reserve(
			std::min<std::size_t>(
				request.MaximumResponseBytes,
				64 * 1024));

		TextWriteState state{
			.Output = &result.Body,
			.Limit =
				request.MaximumResponseBytes,
		};

		auto configured =
			ConfigureRequest(
				request.Url,
				request.MaximumRedirects,
				&WriteText,
				&state);

		if (!configured)
			return std::unexpected(
				configured.error());

		auto handle =
			std::move(*configured);

		const CURLcode perform_result =
			::curl_easy_perform(
				handle.Get());

		if (state.LimitExceeded) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						ResponseTooLarge,
					CURLE_OK,
					0,
					state.ObservedBytes,
					state.Limit));
		}

		if (perform_result != CURLE_OK) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						RequestFailed,
					perform_result));
		}

		auto status =
			ReadStatusCode(
				handle.Get());

		if (!status)
			return std::unexpected(
				status.error());

		result.StatusCode = *status;
		return result;
	}

	std::expected<
		HttpFileResponse,
		HttpError>
	HttpClient::DownloadFile(
		const HttpFileRequest& request) {

		if (request.Destination.empty()) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						EmptyDestination));
		}

		auto valid =
			ValidateRequest(
				request.Url,
				request.MaximumResponseBytes);

		if (!valid)
			return std::unexpected(
				valid.error());

		std::ofstream stream{
			request.Destination,
			std::ios::binary
				| std::ios::trunc
		};

		if (!stream.is_open()) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						DestinationOpenFailed,
					CURLE_OK,
					0,
					0,
					request.MaximumResponseBytes,
					"Unable to open download destination."));
		}

		FileWriteState state{
			.Stream = &stream,
			.Limit =
				request.MaximumResponseBytes,
		};

		auto configured =
			ConfigureRequest(
				request.Url,
				request.MaximumRedirects,
				&WriteFile,
				&state);

		if (!configured)
			return std::unexpected(
				configured.error());

		auto handle =
			std::move(*configured);

		const CURLcode perform_result =
			::curl_easy_perform(
				handle.Get());

		if (state.LimitExceeded) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						ResponseTooLarge,
					CURLE_OK,
					0,
					state.ObservedBytes,
					state.Limit));
		}

		if (state.WriteFailed) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						DestinationWriteFailed,
					CURLE_OK,
					0,
					state.ObservedBytes,
					state.Limit,
					"Unable to write downloaded data."));
		}

		if (perform_result != CURLE_OK) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						RequestFailed,
					perform_result,
					0,
					state.ObservedBytes,
					state.Limit));
		}

		auto status =
			ReadStatusCode(
				handle.Get());

		if (!status)
			return std::unexpected(
				status.error());

		stream.flush();

		if (!stream) {
			return std::unexpected(
				MakeError(
					HttpErrorCode::
						DestinationWriteFailed,
					CURLE_OK,
					*status,
					state.ObservedBytes,
					state.Limit,
					"Unable to flush downloaded data."));
		}

		return HttpFileResponse{
			.StatusCode = *status,
			.BytesWritten =
				state.ObservedBytes,
		};
	}
}
