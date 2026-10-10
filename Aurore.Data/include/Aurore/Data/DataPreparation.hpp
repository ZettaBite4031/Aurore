#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <variant>

namespace Aurore::Data {
	struct MinecraftVersion final {
		std::string Id;
		std::int32_t ProtocolVersion{ 0 };
	};

	struct OfficialDownloadSource final {};

	struct LocalJarSource final {
		std::filesystem::path JarPath;
	};

	using MinecraftSource = std::variant<OfficialDownloadSource, LocalJarSource>;

	struct DataPreparationOptions final {
		MinecraftVersion Version;
		MinecraftSource Source;
		std::filesystem::path CacheRoot;
	};

	enum class DataPreparationErrorCode : std::int8_t {
		EmptyMinecraftVersion,
		InvalidProtocolVersion,
		EmptyCacheRoot,
		EmptyLocalJarPath,
	};

	struct DataPreparationError final {
		DataPreparationErrorCode Code;
	};

	class DataPreparationRequest final {
	public:
		[[nodiscard]] static std::expected<DataPreparationRequest, DataPreparationError> Create(DataPreparationOptions options);

		[[nodiscard]] const MinecraftVersion& GetVersion() const noexcept;
		[[nodiscard]] const MinecraftSource& GetSource() const noexcept;
		[[nodiscard]] const std::filesystem::path& GetCacheRoot() const noexcept;

	private:
		explicit DataPreparationRequest(DataPreparationOptions options) noexcept;
		DataPreparationOptions m_Options;
	};
}
