#include <Aurore/Protocol/ConfigurationSequence.hpp>

#include <Aurore/Protocol/ConfigurationRegistryAdapter.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace {
	namespace Configuration = Aurore::Protocol::Packets::Configuration;

	using Aurore::Protocol::ConfigurationRegistryAdapter;
	using Aurore::Protocol::ConfigurationSequenceError;
	using Aurore::Protocol::ConfigurationSequenceErrorCause;
	using Aurore::Protocol::ConfigurationSequenceErrorCode;
	using Aurore::Protocol::ConfigurationSequencePolicy;
	using Aurore::Protocol::ConfigurationSequenceStage;
	using Aurore::Protocol::RegistryCompatibilityManifest;
	using Aurore::Protocol::RegistryCompatibilityValidator;
	using Aurore::Protocol::RegistryManifestCompleteness;
	using Aurore::Protocol::RegistrySnapshotInventoryAdapter;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::ResourceLocation;

	[[nodiscard]] Aurore::Util::RegistryGeneration Generation(const std::shared_ptr<const RegistrySnapshot>& snapshot) noexcept {
		return snapshot ? snapshot->GetGeneration() : Aurore::Util::NoRegistryGeneration;
	}

	[[nodiscard]] ConfigurationSequenceError MakeError(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		ConfigurationSequenceErrorCode code,
		ConfigurationSequenceStage stage,
		std::optional<ResourceLocation> key = std::nullopt,
		ConfigurationSequenceErrorCause cause = {}) {

		return ConfigurationSequenceError{
			.Code = code,
			.Stage = stage,
			.Generation = Generation(snapshot),
			.Key = std::move(key),
			.Cause = std::move(cause),
		};
	}

	[[nodiscard]] const ResourceLocation* ErrorKey(const Aurore::Protocol::RegistrySnapshotInventoryError& error) { return &error.Key; }
	[[nodiscard]] const ResourceLocation* ErrorKey(const Aurore::Protocol::RegistryInventoryError& error) { return error.Key ? &*error.Key : nullptr; }
	[[nodiscard]] const ResourceLocation* ErrorKey(const Configuration::ContractError&) { return nullptr; }

	[[nodiscard]] const ResourceLocation* ErrorKey(const Configuration::EncodeError& error) {
		if (error.RegistryKey) return &*error.RegistryKey;
		if (error.EntryKey) return &*error.EntryKey;
		if (error.TagKey) return &*error.TagKey;
		if (error.Identifier) return &*error.Identifier;
		return nullptr;
	}

	template<typename T>
	[[nodiscard]] ConfigurationSequenceError MakeCausedError(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		ConfigurationSequenceErrorCode code,
		ConfigurationSequenceStage stage,
		const T& cause) {

		const auto* key = ErrorKey(cause);
		return MakeError(snapshot, code, stage, key ? std::optional{ *key } : std::nullopt, cause);
	}

	[[nodiscard]] constexpr std::size_t EncodedVarIntSize(std::int32_t value) noexcept {
		std::uint32_t remaining = static_cast<std::uint32_t>(value);
		std::size_t result{ 1 };
		while ((remaining & ~0x7Fu) != 0) {
			remaining >>= 7;
			result++;
		}
		return result;
	}

	template<typename T>
	[[nodiscard]] std::expected<Aurore::Protocol::PacketFrame, ConfigurationSequenceError> EncodePacket(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const T& packet,
		ConfigurationSequenceStage stage,
		const Configuration::Limits& limits) {

		auto encoded = Configuration::Clientbound::Encode(packet, limits);
		if (encoded) return std::move(*encoded);
		return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::PacketEncodingFailed, stage, encoded.error()));
	}

	[[nodiscard]] std::expected<Aurore::Protocol::PacketFrame, ConfigurationSequenceError> EncodeFinish(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const Configuration::Limits& limits) {

		const auto encoded_id_size = EncodedVarIntSize(Configuration::Clientbound::FinishConfigurationPacketId);
		if (limits.MaximumPacketDataBytes >= encoded_id_size)
			return Configuration::Clientbound::Encode(Configuration::Clientbound::FinishConfiguration{});

		const Configuration::EncodeError cause{
			.Code = Configuration::EncodeErrorCode::PacketSizeExceeded,
			.ObservedValue = encoded_id_size,
			.LimitValue = limits.MaximumPacketDataBytes,
		};
		return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::PacketEncodingFailed, ConfigurationSequenceStage::FinishConfiguration, cause));
	}

	[[nodiscard]] std::expected<void, ConfigurationSequenceError> ValidateSnapshot(const std::shared_ptr<const RegistrySnapshot>& snapshot) {
		if (!snapshot)
			return std::unexpected(MakeError(snapshot, ConfigurationSequenceErrorCode::MissingSnapshot, ConfigurationSequenceStage::SnapshotValidation));
		if (snapshot->GetGeneration() == Aurore::Util::NoRegistryGeneration)
			return std::unexpected(MakeError(snapshot, ConfigurationSequenceErrorCode::InvalidGeneration, ConfigurationSequenceStage::SnapshotValidation));
		return {};
	}

	[[nodiscard]] std::expected<void, ConfigurationSequenceError> ValidatePolicy(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const ConfigurationSequencePolicy& policy) {

		if (!policy.RegistryManifest)
			return std::unexpected(MakeError(snapshot, ConfigurationSequenceErrorCode::MissingRegistryManifest, ConfigurationSequenceStage::PolicyValidation));
		if (policy.RegistryManifest->GetProtocolVersion() != Configuration::TargetProtocolVersion)
			return std::unexpected(MakeError(snapshot, ConfigurationSequenceErrorCode::ProtocolVersionMismatch, ConfigurationSequenceStage::PolicyValidation));

		for (std::size_t i{ 0 }; i < policy.EnabledFeatures.size(); i++) {
			for (std::size_t existing{ 0 }; existing < i; existing++) {
				if (policy.EnabledFeatures[existing] != policy.EnabledFeatures[i]) continue;
				auto error = MakeError(snapshot, ConfigurationSequenceErrorCode::DuplicateFeatureFlag, ConfigurationSequenceStage::PolicyValidation, policy.EnabledFeatures[i]);
				error.Index = i;
				error.ExistingIndex = existing;
				return std::unexpected(std::move(error));
			}
		}

		for (std::size_t i{ 0 }; i < policy.KnownPacks.size(); i++) {
			for (std::size_t existing{ 0 }; existing < i; existing++) {
				if (policy.KnownPacks[existing] != policy.KnownPacks[i]) continue;
				auto error = MakeError(snapshot, ConfigurationSequenceErrorCode::DuplicateKnownPack, ConfigurationSequenceStage::PolicyValidation);
				error.Pack = policy.KnownPacks[i];
				error.Index = i;
				error.ExistingIndex = existing;
				return std::unexpected(std::move(error));
			}
		}
		return {};
	}

	[[nodiscard]] std::expected<void, ConfigurationSequenceError> ValidateProjection(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const RegistryCompatibilityManifest& manifest) {

		auto inventory = RegistrySnapshotInventoryAdapter::Build(manifest, *snapshot);
		if (inventory) return {};
		return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::SnapshotProjectionFailed, ConfigurationSequenceStage::SnapshotProjection, inventory.error()));
	}

	[[nodiscard]] std::expected<std::vector<Aurore::Protocol::PacketFrame>, ConfigurationSequenceError> BuildInitialFrames(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const ConfigurationSequencePolicy& policy,
		const Configuration::Limits& limits) {

		std::vector<Aurore::Protocol::PacketFrame> frames;
		frames.reserve(2);

		auto features = EncodePacket(snapshot, Configuration::Clientbound::FeatureFlags{ .Features = policy.EnabledFeatures }, ConfigurationSequenceStage::FeatureFlags, limits);
		if (!features) return std::unexpected(features.error());
		frames.push_back(std::move(*features));

		auto packs = EncodePacket(snapshot, Configuration::Clientbound::SelectKnownPacks{ .Packs = policy.KnownPacks }, ConfigurationSequenceStage::KnownPacks, limits);
		if (!packs) return std::unexpected(packs.error());
		frames.push_back(std::move(*packs));
		return frames;
	}

	[[nodiscard]] std::expected<std::vector<Aurore::Protocol::PacketFrame>, ConfigurationSequenceError> BuildPostNegotiationFrames(
		const std::shared_ptr<const RegistrySnapshot>& snapshot,
		const RegistryCompatibilityManifest& manifest,
		std::span<const Configuration::KnownPack> offered,
		std::span<const Configuration::KnownPack> selected,
		const Configuration::Limits& limits) {

		const auto selection = Configuration::ValidateKnownPackSelection(offered, selected);
		if (!selection)
			return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::InvalidKnownPackSelection, ConfigurationSequenceStage::KnownPackSelection, selection.error()));

		auto inventory = RegistrySnapshotInventoryAdapter::Build(manifest, *snapshot);
		if (!inventory)
			return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::SnapshotProjectionFailed, ConfigurationSequenceStage::SnapshotProjection, inventory.error()));

		if (manifest.GetCompleteness() == RegistryManifestCompleteness::Complete) {
			const auto validation = RegistryCompatibilityValidator::Validate(manifest, *inventory, selected);
			if (!validation)
				return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::RegistryValidationFailed, ConfigurationSequenceStage::RegistryValidation, validation.error()));
		}

		std::vector<Aurore::Protocol::PacketFrame> frames;
		frames.reserve(manifest.GetRules().size() + 2);

		Configuration::Clientbound::UpdateTags tags;
		tags.Registries.reserve(manifest.GetRules().size());

		for (const auto& rule : manifest.GetRules()) {
			auto section = ConfigurationRegistryAdapter::Build(rule, *snapshot, selected);
			if (!section)
				return std::unexpected(MakeCausedError(snapshot, ConfigurationSequenceErrorCode::SnapshotProjectionFailed, ConfigurationSequenceStage::SnapshotProjection, section.error()));

			if (section->RegistryData) {
				auto frame = EncodePacket(snapshot, *section->RegistryData, ConfigurationSequenceStage::RegistryData, limits);
				if (!frame) return std::unexpected(frame.error());
				frames.push_back(std::move(*frame));
			}

			if (section->Tags)
				tags.Registries.push_back(std::move(*section->Tags));
		}

		auto tag_frame = EncodePacket(snapshot, tags, ConfigurationSequenceStage::Tags, limits);
		if (!tag_frame) return std::unexpected(tag_frame.error());
		frames.push_back(std::move(*tag_frame));

		auto finish = EncodeFinish(snapshot, limits);
		if (!finish) return std::unexpected(finish.error());
		frames.push_back(std::move(*finish));
		return frames;
	}
}

namespace Aurore::Protocol {
	ConfigurationTransmissionPlan::ConfigurationTransmissionPlan(
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
		std::shared_ptr<const RegistryCompatibilityManifest> manifest,
		Packets::Configuration::Limits limits,
		std::vector<PacketFrame> initial_frames,
		std::vector<Packets::Configuration::KnownPack> offered_known_packs) noexcept
		: m_Snapshot(std::move(snapshot)), m_Manifest(std::move(manifest)), m_Limits(limits),
		m_InitialFrames(std::move(initial_frames)), m_OfferedKnownPacks(std::move(offered_known_packs)) {}

	Aurore::Util::RegistryGeneration ConfigurationTransmissionPlan::GetGeneration() const noexcept {
		return m_Snapshot ? m_Snapshot->GetGeneration() : Aurore::Util::NoRegistryGeneration;
	}

	const std::shared_ptr<const Aurore::Util::RegistrySnapshot>& ConfigurationTransmissionPlan::GetSnapshot() const noexcept {
		return m_Snapshot;
	}

	std::span<const PacketFrame> ConfigurationTransmissionPlan::GetInitialFrames() const noexcept {
		return m_InitialFrames;
	}

	std::span<const Packets::Configuration::KnownPack> ConfigurationTransmissionPlan::GetOfferedKnownPacks() const noexcept {
		return m_OfferedKnownPacks;
	}

	std::vector<PacketFrame> ConfigurationTransmissionPlan::ReleaseInitialFrames() noexcept {
		if (m_ConsumptionState != ConsumptionState::Ready) return {};
		m_ConsumptionState = ConsumptionState::InitialReleased;
		return std::move(m_InitialFrames);
	}

	std::expected<std::vector<PacketFrame>, ConfigurationSequenceError> ConfigurationTransmissionPlan::MaterializePostNegotiationFrames(
		std::span<const Packets::Configuration::KnownPack> selected_known_packs) {

		if (m_ConsumptionState != ConsumptionState::InitialReleased)
			return std::unexpected(MakeError(m_Snapshot, ConfigurationSequenceErrorCode::InvalidPlanState, ConfigurationSequenceStage::KnownPackSelection));
		if (!m_Manifest)
			return std::unexpected(MakeError(m_Snapshot, ConfigurationSequenceErrorCode::MissingRegistryManifest, ConfigurationSequenceStage::PolicyValidation));

		auto frames = BuildPostNegotiationFrames(m_Snapshot, *m_Manifest, m_OfferedKnownPacks, selected_known_packs, m_Limits);
		if (!frames) return std::unexpected(frames.error());

		m_ConsumptionState = ConsumptionState::Complete;
		return frames;
	}

	std::expected<ConfigurationTransmissionPlan, ConfigurationSequenceError> ConfigurationSequenceBuilder::Build(
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
		const ConfigurationSequencePolicy& policy,
		const Packets::Configuration::Limits& limits) {

		if (const auto validation = ValidateSnapshot(snapshot); !validation) return std::unexpected(validation.error());
		if (const auto validation = ValidatePolicy(snapshot, policy); !validation) return std::unexpected(validation.error());
		if (const auto validation = ValidateProjection(snapshot, *policy.RegistryManifest); !validation) return std::unexpected(validation.error());

		auto initial_frames = BuildInitialFrames(snapshot, policy, limits);
		if (!initial_frames) return std::unexpected(initial_frames.error());

		return ConfigurationTransmissionPlan{
			std::move(snapshot),
			policy.RegistryManifest,
			limits,
			std::move(*initial_frames),
			policy.KnownPacks,
		};
	}
}
