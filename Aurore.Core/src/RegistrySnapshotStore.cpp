#include <Aurore/Core/RegistrySnapshotStore.hpp>

#include <utility>

namespace Aurore::Core {
	std::shared_ptr<const Aurore::Util::RegistrySnapshot> RegistrySnapshotStore::GetActiveSnapshot() const noexcept {
		return m_ActiveSnapshot.load(std::memory_order_acquire);
	}

	Aurore::Util::RegistryGeneration RegistrySnapshotStore::GetActiveGeneration() const noexcept {
		const auto snapshot = GetActiveSnapshot();
		return snapshot == nullptr ? Aurore::Util::NoRegistryGeneration : snapshot->GetGeneration();
	}

	std::expected<void, RegistryPublicationError> RegistrySnapshotStore::Publish(
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot) noexcept {

		if (snapshot == nullptr)
			return std::unexpected(RegistryPublicationError{
				.Code = RegistryPublicationErrorCode::NullSnapshot,
				.AttemptedGeneration = Aurore::Util::NoRegistryGeneration,
				.ActiveGeneration = GetActiveGeneration(),
				});

		const auto attempted_generation = snapshot->GetGeneration();
		auto active = m_ActiveSnapshot.load(std::memory_order_acquire);

		while (true) {
			const auto active_generation = active == nullptr
				? Aurore::Util::NoRegistryGeneration
				: active->GetGeneration();

			if (attempted_generation <= active_generation)
				return std::unexpected(RegistryPublicationError{
					.Code = RegistryPublicationErrorCode::GenerationNotNewer,
					.AttemptedGeneration = attempted_generation,
					.ActiveGeneration = active_generation,
					});

			if (m_ActiveSnapshot.compare_exchange_weak(
				active,
				snapshot,
				std::memory_order_release,
				std::memory_order_acquire)) return {};
		}
	}
}

