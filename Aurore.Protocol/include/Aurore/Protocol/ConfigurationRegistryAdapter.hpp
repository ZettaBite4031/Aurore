#pragma once

#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <expected>
#include <optional>
#include <span>

namespace Aurore::Protocol {
	struct ConfigurationRegistrySection final {
		std::optional<Packets::Configuration::Clientbound::RegistryData> RegistryData;
		std::optional<Packets::Configuration::Clientbound::RegistryTagData> Tags;
	};

	class ConfigurationRegistryAdapter final {
	public:
		ConfigurationRegistryAdapter() = delete;

		[[nodiscard]] static std::expected<ConfigurationRegistrySection, RegistrySnapshotInventoryError> Build(
			const RegistryCompatibilityRule& rule,
			const Aurore::Util::RegistrySnapshot& snapshot,
			std::span<const Packets::Configuration::KnownPack> selected_known_packs = {});
	};
}
