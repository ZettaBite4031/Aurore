#include "HttpClient.hpp"

#include <Aurore/Build/Config.hpp>

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

static_assert(Aurore::Build::IsWindows, "Windows HTTP backend compiled for non-Windows target.");

namespace {
	using Aurore::Data::Detail::HttpError;
	using Aurore::Data::Detail::HttpErrorCode;
	using Aurore::Data::Detail::HttpTextRequest;
	using Aurore::Data::Detail::HttpTextResponse;
	using Aurore::Data::Detail::MaximumHttpUrlBytes;

	class UniqueInternetHandle final {
	public:
		UniqueInternetHandle() noexcept = default;
		explicit UniqueInternetHandle(HINTERNET handle) noexcept
			: m_Handle(handle) {}

		~UniqueInternetHandle() { Reset(); }

		UniqueInternetHandle(const UniqueInternetHandle&) = delete;
		UniqueInternetHandle& operator=(const UniqueInternetHandle&) = delete;

		UniqueInternetHandle(UniqueInternetHandle&& other) noexcept
			: m_Handle(std::exchange(other.m_Handle, nullptr)) {}

		UniqueInternetHandle& operator=(UniqueInternetHandle&& other) noexcept {
			if (this == &other) return *this;

			Reset();
			m_Handle = std::exchange(other.m_Handle, nullptr);
			return *this;
		}

		[[nodiscard]] HINTERNET Get() const noexcept { return m_Handle; }
		[[nodiscard]] explicit operator bool() const noexcept { return m_Handle != nullptr; }

		void Reset(HINTERNET handle = nullptr) noexcept {
			if (m_Handle != nullptr) ::WinHttpCloseHandle(m_Handle);
			m_Handle = handle;
		}

	private:
		HINTERNET m_Handle{ nullptr };
	};

	[[nodiscard]] HttpError MakeError(HttpErrorCode code, DWORD native_code = ERROR_SUCCESS, std::uint32_t status_code = 0, std::size_t observed = 0, std::size_t limit = 0) {
		std::string message;
		if (native_code != ERROR_SUCCESS)
			message = std::system_category().message(static_cast<int>(native_code));

		return HttpError{
			.Code = code,
			.StatusCode = status_code,
			.NativeCode = static_cast<std::uint64_t>(native_code),
			.ObservedBytes = observed,
			.LimitBytes = limit,
			.Message = std::move(message),
		};
	}

	[[nodiscard]] std::expected<std::wstring, HttpError> Utf8ToWide(std::string_view value) {
		if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			return std::unexpected(MakeError(HttpErrorCode::UrlTooLong));

		const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (required <= 0)
			return std::unexpected(MakeError(HttpErrorCode::InvalidUrl, ::GetLastError()));

		std::wstring result(static_cast<std::size_t>(required), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required) <= 0)
			return std::unexpected(MakeError(HttpErrorCode::InvalidUrl, ::GetLastError()));

		return result;
	}

	struct ParsedUrl final {
		std::wstring Host;
		std::wstring Path;
		INTERNET_PORT Port{ 0 };
	};

	[[nodiscard]] std::expected<ParsedUrl, HttpError> ParseHttpsUrl(std::wstring& url) {
		URL_COMPONENTS components{};
		components.dwStructSize = sizeof(components);
		components.dwHostNameLength = static_cast<DWORD>(-1);
		components.dwUrlPathLength = static_cast<DWORD>(-1);
		components.dwExtraInfoLength = static_cast<DWORD>(-1);
		if (!::WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components))
			return std::unexpected(MakeError(HttpErrorCode::InvalidUrl, ::GetLastError()));

		if (components.nScheme != INTERNET_SCHEME_HTTPS)
			return std::unexpected(MakeError(HttpErrorCode::InsecureUrl));

		if (components.lpszHostName == nullptr || components.dwHostNameLength == 0)
			return std::unexpected(MakeError(HttpErrorCode::InvalidUrl));

		std::wstring host{
			components.lpszHostName,
			components.dwHostNameLength,
		};
		std::wstring path;

		if (components.lpszUrlPath != nullptr && components.dwUrlPathLength != 0)
			path.assign(components.lpszUrlPath, components.dwUrlPathLength);
		else path = L"/";

		if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength)
			path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

		return ParsedUrl{
			.Host = std::move(host),
			.Path = std::move(path),
			.Port = components.nPort,
		};
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

		auto wide_url = Utf8ToWide(request.Url);
		if (!wide_url) return std::unexpected(wide_url.error());

		auto parsed = ParseHttpsUrl(*wide_url);
		if (!parsed) return std::unexpected(parsed.error());

		UniqueInternetHandle session{
			::WinHttpOpen(
				L"Aurore/0.1",
				WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0)
		};

		if (!session)
			return std::unexpected(MakeError(HttpErrorCode::InitializationFailed, ::GetLastError()));

		if (!::WinHttpSetTimeouts(session.Get(), 10'000, 10'000, 10'000, 30'000))
			return std::unexpected(MakeError(HttpErrorCode::InitializationFailed, ::GetLastError()));

		UniqueInternetHandle connection{
			::WinHttpConnect(
				session.Get(),
				parsed->Host.c_str(),
				parsed->Port,
				0)
		};

		if (!connection)
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		UniqueInternetHandle http_request{
			::WinHttpOpenRequest(
				connection.Get(),
				L"GET",
				parsed->Path.c_str(),
				nullptr,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				WINHTTP_FLAG_SECURE)
		};

		if (!http_request)
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		DWORD maximum_redirects = request.MaximumRedirects;
		if (!::WinHttpSetOption(http_request.Get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maximum_redirects, sizeof(maximum_redirects)))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
		if (!::WinHttpSetOption(http_request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy)))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		if (!::WinHttpSendRequest(http_request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		if (!::WinHttpReceiveResponse(http_request.Get(), nullptr))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		DWORD status_code{ 0 };
		DWORD status_size{ sizeof(status_code) };
		if (!::WinHttpQueryHeaders(http_request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
			return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError()));

		if (status_code < 200 || status_code >= 300)
			return std::unexpected(MakeError(HttpErrorCode::HttpStatusFailed, ERROR_SUCCESS, status_code));

		HttpTextResponse result{ .StatusCode = status_code };

		result.Body.reserve(std::min<std::size_t>(request.MaximumResponseBytes, 64 * 1024));
		std::array<char, 16 * 1024> buffer{};
		for (;;) {
			DWORD bytes_read{ 0 };
			if (!::WinHttpReadData(http_request.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read))
				return std::unexpected(MakeError(HttpErrorCode::RequestFailed, ::GetLastError(), status_code));

			if (bytes_read == 0) break;

			const auto size = static_cast<std::size_t>(bytes_read);
			if (size > request.MaximumResponseBytes - result.Body.size())
				return std::unexpected(MakeError(HttpErrorCode::ResponseTooLarge, ERROR_SUCCESS, status_code, result.Body.size() + size, request.MaximumResponseBytes));

			result.Body.append(buffer.data(), size);
		}

		return result;
	}
}
