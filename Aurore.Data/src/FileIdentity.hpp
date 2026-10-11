#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace Aurore::Data::Detail {
	struct FileIdentity final {
		std::uintmax_t Size{ 0 };
		std::string Sha1;
	};

	enum class FileIdentityErrorCode : std::uint8_t {
		FileOpenFailed,
		FileReadFailed,
		EmptyFile,
	};

	struct FileIdentityError final {
		FileIdentityErrorCode Code;

		std::filesystem::path Path;
		std::error_code SystemError;
	};

	[[nodiscard]] std::expected<FileIdentity, FileIdentityError> ReadFileIdentity(const std::filesystem::path& path);
}
