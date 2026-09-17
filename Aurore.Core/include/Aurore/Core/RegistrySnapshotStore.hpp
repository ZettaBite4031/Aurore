#pragma once

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <atomic>
#include <compare>
#include <cstdint>
#include <expected>
#include <memory>

namespace Aurore::Core {
	enum class RegistryPublicationErrorCode : std::uint8_t {
		NullSnapshot,
		GenerationNotNewer,
	};

	struct RegistryPublicationError final {
		RegistryPublicationErrorCode Code;
		Aurore::Util::RegistryGeneration AttemptedGeneration{ Aurore::Util::NoRegistryGeneration };
		Aurore::Util::RegistryGeneration ActiveGeneration{ Aurore::Util::NoRegistryGeneration };

		auto operator<=>(const RegistryPublicationError&) const noexcept = default;
	};

	class RegistrySnapshotStore final {
	public:
		RegistrySnapshotStore() = default;

		RegistrySnapshotStore(const RegistrySnapshotStore&) = delete;
		RegistrySnapshotStore& operator=(const RegistrySnapshotStore&) = delete;
		RegistrySnapshotStore(RegistrySnapshotStore&&) = delete;
		RegistrySnapshotStore& operator=(RegistrySnapshotStore&&) = delete;

		[[nodiscard]] std::shared_ptr<const Aurore::Util::RegistrySnapshot> GetActiveSnapshot() const noexcept;
		[[nodiscard]] Aurore::Util::RegistryGeneration GetActiveGeneration() const noexcept;

		[[nodiscard]] std::expected<void, RegistryPublicationError> Publish(
			std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot) noexcept;

	private:
		std::atomic<std::shared_ptr<const Aurore::Util::RegistrySnapshot>> m_ActiveSnapshot;
	};
}

