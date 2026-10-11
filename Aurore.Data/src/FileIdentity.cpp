#include "FileIdentity.hpp"

#include "Sha1.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <span>
#include <utility>

namespace {
	using Aurore::Data::Detail::FileIdentityError;
	using Aurore::Data::Detail::FileIdentityErrorCode;

	[[nodiscard]] FileIdentityError MakeError(FileIdentityErrorCode code, std::filesystem::path path, std::error_code system_error = {}) {
		return FileIdentityError{ .Code = code, .Path = std::move(path), .SystemError = system_error };
	}
}

namespace Aurore::Data::Detail {
	std::expected<FileIdentity, FileIdentityError> ReadFileIdentity(const std::filesystem::path& path) {
		std::ifstream stream{ path, std::ios::binary };
		if (!stream.is_open())
			return std::unexpected(MakeError(FileIdentityErrorCode::FileOpenFailed, path));

		Sha1 hasher;
		std::array<char, 64 * 1024> buffer;
		std::uintmax_t total_size{ 0 };
		while (stream) {
			stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto count = stream.gcount();
			if (count <= 0) continue;
			const auto size = static_cast<std::size_t>(count);
			hasher.Update(std::as_bytes(std::span{ buffer.data(), size }));
			total_size += static_cast<std::uintmax_t>(size);
		}

		if (!stream.eof())
			return std::unexpected(MakeError(FileIdentityErrorCode::FileReadFailed, path));

		if (total_size == 0)
			return std::unexpected(MakeError(FileIdentityErrorCode::EmptyFile, path));

		return FileIdentity{
			.Size = total_size,
			.Sha1 = ToHex(hasher.Finalize()),
		};
	}
}
