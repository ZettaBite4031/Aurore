#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/RegistryCompatibility.hpp>

#include <Aurore/Util/RegistrySnapshot.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Aurore::Protocol {
	struct ConfigurationSequencePolicy final {
		std::vector<Aurore::Util::ResourceLocation> EnabledFeatures;
		std::vector<Packets::Configuration::KnownPack> KnownPacks;
		std::shared_ptr<const RegistryCompatibilityManifest> RegistryManifest;

		auto operator<=>(const ConfigurationSequencePolicy&) const noexcept = default;
	};

	enum class ConfigurationSequenceStage : std::uint8_t {
		SnapshotValidation,
		PolicyValidation,
		SnapshotProjection,
		FeatureFlags,
		KnownPacks,
		KnownPackSelection,
		RegistryValidation,
		RegistryData,
		Tags,
		FinishConfiguration,
	};

	enum class ConfigurationSequenceErrorCode : std::uint8_t {
		MissingSnapshot,
		InvalidGeneration,
		MissingRegistryManifest,
		ProtocolVersionMismatch,
		DuplicateFeatureFlag,
		DuplicateKnownPack,
		SnapshotProjectionFailed,
		InvalidKnownPackSelection,
		RegistryValidationFailed,
		PacketEncodingFailed,
		InvalidPlanState,
	};

	using ConfigurationSequenceErrorCause = std::variant<
		std::monostate,
		Packets::Configuration::ContractError,
		RegistrySnapshotInventoryError,
		RegistryInventoryError,
		Packets::Configuration::EncodeError>;

	struct ConfigurationSequenceError final {
		ConfigurationSequenceErrorCode Code;
		ConfigurationSequenceStage Stage;
		Aurore::Util::RegistryGeneration Generation{ Aurore::Util::NoRegistryGeneration };
		std::optional<Aurore::Util::ResourceLocation> Key;
		std::optional<Packets::Configuration::KnownPack> Pack;
		std::optional<std::size_t> Index;
		std::optional<std::size_t> ExistingIndex;
		ConfigurationSequenceErrorCause Cause;

		auto operator<=>(const ConfigurationSequenceError&) const noexcept = default;
	};

	class ConfigurationTransmissionPlan final {
	public:
		ConfigurationTransmissionPlan(const ConfigurationTransmissionPlan&) = delete;
		ConfigurationTransmissionPlan& operator=(const ConfigurationTransmissionPlan&) = delete;
		ConfigurationTransmissionPlan(ConfigurationTransmissionPlan&&) noexcept = default;
		ConfigurationTransmissionPlan& operator=(ConfigurationTransmissionPlan&&) noexcept = default;
		~ConfigurationTransmissionPlan() = default;

		[[nodiscard]] Aurore::Util::RegistryGeneration GetGeneration() const noexcept;
		[[nodiscard]] const std::shared_ptr<const Aurore::Util::RegistrySnapshot>& GetSnapshot() const noexcept;
		[[nodiscard]] std::span<const PacketFrame> GetInitialFrames() const noexcept;
		[[nodiscard]] std::span<const Packets::Configuration::KnownPack> GetOfferedKnownPacks() const noexcept;

		[[nodiscard]] std::vector<PacketFrame> ReleaseInitialFrames() noexcept;
		[[nodiscard]] std::expected<std::vector<PacketFrame>, ConfigurationSequenceError> MaterializePostNegotiationFrames(
			std::span<const Packets::Configuration::KnownPack> selected_known_packs);

	private:
		enum class ConsumptionState : std::uint8_t {
			Ready,
			InitialReleased,
			Complete,
		};

		ConfigurationTransmissionPlan(
			std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
			std::shared_ptr<const RegistryCompatibilityManifest> manifest,
			Packets::Configuration::Limits limits,
			std::vector<PacketFrame> initial_frames,
			std::vector<Packets::Configuration::KnownPack> offered_known_packs) noexcept;

		std::shared_ptr<const Aurore::Util::RegistrySnapshot> m_Snapshot;
		std::shared_ptr<const RegistryCompatibilityManifest> m_Manifest;
		Packets::Configuration::Limits m_Limits;
		std::vector<PacketFrame> m_InitialFrames;
		std::vector<Packets::Configuration::KnownPack> m_OfferedKnownPacks;
		ConsumptionState m_ConsumptionState{ ConsumptionState::Ready };

		friend class ConfigurationSequenceBuilder;
	};

	class ConfigurationSequenceBuilder final {
	public:
		[[nodiscard]] static std::expected<ConfigurationTransmissionPlan, ConfigurationSequenceError> Build(
			std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
			const ConfigurationSequencePolicy& policy,
			const Packets::Configuration::Limits& limits = Packets::Configuration::DefaultLimits);
	};
}
