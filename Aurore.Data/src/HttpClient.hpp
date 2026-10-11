#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace Aurore::Data::Detail {
	inline constexpr std::size_t DefaultMaximumHttpTextBytes{ 4 * 1024 * 1024 };
	inline constexpr std::size_t MaximumHttpUrlBytes{ 16 * 1024 };

	struct HttpTextRequest final {
		std::string Url;

		std::size_t MaximumResponseBytes{ DefaultMaximumHttpTextBytes };

		std::uint32_t MaximumRedirects{ 5 };
	};

	struct HttpTextResponse final {
		std::uint32_t StatusCode{ 0 };
		std::string Body;
	};

	enum class HttpErrorCode : std::uint8_t {
		PlatformNotSupported,

		EmptyUrl,
		UrlTooLong,
		InsecureUrl,
		InvalidResponseLimit,
		InvalidUrl,

		InitializationFailed,
		RequestFailed,
		ResponseTooLarge,
		HttpStatusFailed,
	};

	struct HttpError final {
		HttpErrorCode Code;

		std::uint32_t StatusCode{ 0 };
		std::uint64_t NativeCode{ 0 };

		std::size_t ObservedBytes{ 0 };
		std::size_t LimitBytes{ 0 };

		std::string Message;
	};

	class HttpClient final {
	public:
		[[nodiscard]] static std::expected<HttpTextResponse, HttpError> GetText(const HttpTextRequest& request);
	};
}
