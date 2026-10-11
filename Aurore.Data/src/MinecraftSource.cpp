#include <Aurore/Data/MinecraftSource.hpp>

#include "FileIdentity.hpp"
#include "MojangMetadata.hpp"
#include "OfficialMinecraftSource.hpp"

#include <string>
#include <utility>
#include <variant>

namespace {
	using Aurore::Data::MinecraftSourceArtifact;
	using Aurore::Data::MinecraftSourceError;
	using Aurore::Data::MinecraftSourceErrorCode;
	using Aurore::Data::MinecraftSourceKind;
	using Aurore::Data::Detail::FileIdentityError;
	using Aurore::Data::Detail::MojangMetadataError;
	using Aurore::Data::Detail::OfficialMinecraftSourceError;

	[[nodiscard]] MinecraftSourceError MakeError(MinecraftSourceErrorCode code, std::filesystem::path path = {}, std::error_code system_error = {}, std::string detail = {}) {
		return MinecraftSourceError{ .Code = code, .Path = std::move(path), .SystemError = system_error, .Detail = std::move(detail), };
	}

	[[nodiscard]] MinecraftSourceError TranslateIdentityError(const FileIdentityError& error) {
		using Aurore::Data::Detail::FileIdentityErrorCode;
		MinecraftSourceErrorCode code;

		switch (error.Code) {
		case FileIdentityErrorCode::FileOpenFailed: code = MinecraftSourceErrorCode::FileOpenFailed; break;
		case FileIdentityErrorCode::FileReadFailed: code = MinecraftSourceErrorCode::FileReadFailed; break;
		case FileIdentityErrorCode::EmptyFile: code = MinecraftSourceErrorCode::EmptyFile; break;
		}
		return MakeError(code, error.Path, error.SystemError);
	}

	[[nodiscard]] std::string DescribeMetadataError(const MojangMetadataError& error) {
		if (!error.Detail.empty()) return error.Detail;
		if (!error.Field.empty()) return "Mojang metadata field '" + error.Field + "' was invalid.";
		if (error.HttpFailure && !error.HttpFailure->Message.empty())  return error.HttpFailure->Message;
		return "official Minecraft metadata resolution failed";
	}

	[[nodiscard]] std::string DescribeAcquisitionError(const OfficialMinecraftSourceError& error) {
		if (error.HttpFailure && !error.HttpFailure->Message.empty()) return error.HttpFailure->Message;
		if (!error.ExpectedSha1.empty() || !error.ObservedSha1.empty()) return "downloaded server artifact failed SHA-1 verification";
		if (error.ExpectedSize != 0 || error.ObservedSize != 0) return "downloaded server artifact failed size verification";
		return "official Minecraft server artifact acquisition failed";
	}

	[[nodiscard]] std::expected<MinecraftSourceArtifact, MinecraftSourceError> ResolveLocal(const Aurore::Data::DataPreparationRequest& request, const Aurore::Data::LocalJarSource& local) {
		std::error_code filesystem_error;

		auto resolved_path = std::filesystem::absolute(local.JarPath, filesystem_error);
		if (filesystem_error) 
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathResolutionFailed, local.JarPath, filesystem_error));

		resolved_path = resolved_path.lexically_normal();
		const bool exists = std::filesystem::exists(resolved_path, filesystem_error);
		if (filesystem_error) 
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathInspectionFailed, local.JarPath, filesystem_error));

		if (!exists) 
			return std::unexpected(MakeError(MinecraftSourceErrorCode::FileNotFound, local.JarPath));

		const bool regular_file = std::filesystem::is_regular_file(resolved_path, filesystem_error);
		if (filesystem_error) 
			return std::unexpected(MakeError(MinecraftSourceErrorCode::PathInspectionFailed, local.JarPath, filesystem_error));

		if (!regular_file) 
			return std::unexpected(MakeError(MinecraftSourceErrorCode::NotRegularFile, local.JarPath));

		auto identity = Aurore::Data::Detail::ReadFileIdentity(resolved_path);
		if (!identity) 
			return std::unexpected(TranslateIdentityError(identity.error()));

		return MinecraftSourceArtifact{
			.RequestedVersion = request.GetVersion(),
			.JarPath = std::move(resolved_path),
			.SourceKind = MinecraftSourceKind::LocalJar,
			.FileSize = identity->Size,
			.Sha1 = std::move(identity->Sha1),
			.RequiredJavaMajorVersion = std::nullopt,
			.OfficiallyVerified = false,
		};
	}
}

namespace Aurore::Data {
	std::expected<MinecraftSourceArtifact, MinecraftSourceError> MinecraftSourceResolver::Resolve(const DataPreparationRequest& request) {
		if (const auto* local = std::get_if<LocalJarSource>(&request.GetSource())) 
			return ResolveLocal(request, *local);

		if (std::holds_alternative<OfficialDownloadSource>(request.GetSource())) {
			auto metadata = Detail::MojangMetadataResolver::Resolve(request.GetVersion());
			if (!metadata) 
				return std::unexpected(MakeError(MinecraftSourceErrorCode::OfficialMetadataResolutionFailed, {}, {}, DescribeMetadataError(metadata.error())));

			auto artifact = Detail::OfficialMinecraftSourceAcquirer::Acquire(*metadata, request.GetCacheRoot());
			if (!artifact)
				return std::unexpected(MakeError(MinecraftSourceErrorCode::OfficialArtifactAcquisitionFailed, artifact.error().Path, artifact.error().SystemError, DescribeAcquisitionError(artifact.error())));

			return *artifact;
		}

		return std::unexpected(MakeError(MinecraftSourceErrorCode::SourceNotImplemented));
	}
}
