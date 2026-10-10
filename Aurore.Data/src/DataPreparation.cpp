#include <Aurore/Data/DataPreparation.hpp>

#include <utility>

namespace Aurore::Data {
	std::expected<DataPreparationRequest, DataPreparationError> DataPreparationRequest::Create(DataPreparationOptions options) {
		if (options.Version.Id.empty())
			return std::unexpected(DataPreparationError{ .Code = DataPreparationErrorCode::EmptyMinecraftVersion });

		if (options.Version.ProtocolVersion <= 0)
			return std::unexpected(DataPreparationError{ .Code = DataPreparationErrorCode::InvalidProtocolVersion });

		if (options.CacheRoot.empty())
			return std::unexpected(DataPreparationError{ .Code = DataPreparationErrorCode::EmptyCacheRoot });

		if (const auto* local = std::get_if<LocalJarSource>(&options.Source); local != nullptr && local->JarPath.empty())
			return std::unexpected(DataPreparationError{ .Code = DataPreparationErrorCode::EmptyLocalJarPath });

		/*
			This layer validates only the structural request.

			Filesystem existence, JAR identity, hashes, version matching,
			and soruce provenance belong to source acquisition/verification
			once those responsibilities exist.
		*/
		return DataPreparationRequest{ std::move(options) };
	}

	DataPreparationRequest::DataPreparationRequest(DataPreparationOptions options) noexcept
		: m_Options(std::move(options)) {}

	const MinecraftVersion& DataPreparationRequest::GetVersion() const noexcept { return m_Options.Version; }
	const MinecraftSource& DataPreparationRequest::GetSource() const noexcept { return m_Options.Source; }
	const std::filesystem::path& DataPreparationRequest::GetCacheRoot() const noexcept { return m_Options.CacheRoot; }

}
