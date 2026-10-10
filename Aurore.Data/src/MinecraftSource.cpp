#include <Aurore/Data/MinecraftSource.hpp>

#include "Sha1.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <span>
#include <utility>
#include <variant>

namespace {
	using Aurore::Data::MinecraftSourceError;
	using Aurore::Data::MinecraftSourceErrorCode;

	struct FileIdentity final {
		std::uintmax_t Size{ 0 };
		std::string Sha1;
	};

	[[nodiscard]] MinecraftSourceError MakeError(MinecraftSourceErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}) {
		return MinecraftSourceError{ .Code = code, .Path = std::move(path), .SystemError = system_error };
	}

	[[nodiscard]] std::expected<FileIdentity, MinecraftSourceError> ReadFileIdentity(const std::filesystem::path& path) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open())
			return std::unexpected(MakeError(MinecraftSourceErrorCode::FileOpenFailed, path));

		Aurore::Data::Detail::Sha1 hasher;
		std::array<char, 64 * 1024> buffer{};
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
			return std::unexpected(MakeError(MinecraftSourceErrorCode::FileReadFailed, path));

		if (total_size == 0)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::EmptyFile, path));

		return FileIdentity{
			.Size = total_size,
			.Sha1 = Aurore::Data::Detail::ToHex(hasher.Finalize())
		};
	}
}

namespace Aurore::Data {
	std::expected<MinecraftSourceArtifact, MinecraftSourceError> MinecraftSourceResolver::Resolve(const DataPreparationRequest& request) {
		const auto* local = std::get_if<LocalJarSource>(&request.GetSource());
		if (local == nullptr)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::SourceNotImplemented));

		std::error_code filesystem_error;
		auto resolved_path = std::filesystem::absolute(local->JarPath, filesystem_error);
		if (filesystem_error)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathResolutionFailed, local->JarPath, filesystem_error));

		resolved_path = resolved_path.lexically_normal();
		const bool exists = std::filesystem::exists(resolved_path, filesystem_error);
		if (filesystem_error)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathInspectionFailed, local->JarPath, filesystem_error));

		if (!exists)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::FileNotFound, local->JarPath));

		const bool regular_file = std::filesystem::is_regular_file(resolved_path, filesystem_error);
		if (filesystem_error)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathInspectionFailed, local->JarPath, filesystem_error));
		if (!regular_file)
			return std::unexpected(MakeError(MinecraftSourceErrorCode::NotRegularFile, local->JarPath, filesystem_error));

		auto identity = ReadFileIdentity(resolved_path);
		if (!identity)
			return std::unexpected(std::move(identity.error()));

		return MinecraftSourceArtifact{
			.RequestedVersion = request.GetVersion(),
			.JarPath = std::move(resolved_path),
			.SourceKind = MinecraftSourceKind::LocalJar,
			.FileSize = identity->Size,
			.Sha1 = std::move(identity->Sha1),
		};
	}
}
