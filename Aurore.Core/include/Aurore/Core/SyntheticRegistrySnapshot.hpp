#pragma once

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <variant>

namespace Aurore::Core {
	inline constexpr Aurore::Util::RegistryGeneration InitialSyntheticRegistryGeneration{ 1 };

	enum class SyntheticRegistrySnapshotErrorCode : std::uint8_t {
		InvalidGeneration,
		InvalidResourceLocation,
		RegistryDeclarationFailed,
		TagDeclarationFailed,
		SnapshotBuildFailed,
	};

	using SyntheticRegistrySnapshotErrorCause = std::variant<
		std::monostate,
		Aurore::Util::ResourceLocationError,
		Aurore::Util::RegistryDeclarationError,
		Aurore::Util::RegistryTagDeclarationError,
		Aurore::Util::RegistrySnapshotError>;

	struct SyntheticRegistrySnapshotError final {
		SyntheticRegistrySnapshotErrorCode Code;
		std::optional<Aurore::Util::RegistryKind> Registry;
		std::optional<Aurore::Util::ResourceLocation> Key;
		SyntheticRegistrySnapshotErrorCause Cause;
	};

	class SyntheticRegistrySnapshotFactory final {
	public:
		[[nodiscard]] static std::expected<
			std::shared_ptr<const Aurore::Util::RegistrySnapshot>,
			SyntheticRegistrySnapshotError>
		Build(Aurore::Util::RegistryGeneration generation);
	};
}
