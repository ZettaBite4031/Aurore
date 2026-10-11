#pragma once

#include <Aurore/Core/Server.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Aurore::Server {
	inline constexpr std::string_view MinecraftVersion{ "1.21.11" };
	inline constexpr std::int32_t MinecraftProtocolVersion{ 774 };
	inline constexpr Aurore::Util::RegistryGeneration InitialRegistryGeneration{ 1 };

	struct ProductionBootstrapOptions final {
		std::optional<std::filesystem::path> ExistingCorpusRoot;
		std::filesystem::path CacheRoot{ "cache/minecraft" };
		std::optional<std::filesystem::path> JavaExecutable;
	};

	enum class ProductionBootstrapErrorCode : std::uint8_t {
		InvalidExistingCorpusRoot,
		InvalidPreparationRequest,
		DataPreparationFailed,
		CorpusConstructionFailed,
		SnapshotImportFailed,
	};

	struct ProductionBootstrapError final {
		ProductionBootstrapErrorCode Code;
		std::string Message;
		std::filesystem::path Path;
		std::string Detail;
	};

	struct ProductionBootstrapResult final {
		Aurore::Core::ServerBootstrap Bootstrap;
		std::filesystem::path CorpusRoot;
		bool ReusedExistingCorpus{ false };
	};

	class ProductionBootstrap final {
	public:
		ProductionBootstrap() = delete;

		[[nodiscard]] static std::expected<ProductionBootstrapResult, ProductionBootstrapError> Build(const ProductionBootstrapOptions& options = {});
	};
}
