#include <Aurore/Protocol/ConfigurationSequence.hpp>

#include <string_view>
#include <utility>

namespace Aurore::Protocol::Detail {
	namespace Configuration = Packets::Configuration;

	using Aurore::Util::RegistryGeneration;
	using Aurore::Util::RegistryKind;
	using Aurore::Util::RegistrySnapshot;
	using Aurore::Util::ResourceLocation;

	struct WireRegistryKeys final {
		ResourceLocation Blocks;
		ResourceLocation Items;
		ResourceLocation Biomes;
		ResourceLocation DimensionTypes;
	};

	[[nodiscard]] constexpr std::size_t EncodedVarIntSize(std::int32_t value) noexcept {
		std::uint32_t remaining = static_cast<std::uint32_t>(value);
		std::size_t result{ 1 };
		while ((remaining & ~0x7Fu) != 0) {
			remaining >>= 7;
			result++;
		}
		return result;
	}

	class ConfigurationSequenceAssembler final {
	public:
		ConfigurationSequenceAssembler(
			std::shared_ptr<const RegistrySnapshot> snapshot,
			const ConfigurationSequencePolicy& policy,
			const Configuration::Limits& limits) noexcept
			: m_Snapshot(std::move(snapshot)),
			m_Policy(policy), m_Limits(limits) {}

		[[nodiscard]] std::expected<ConfigurationTransmissionPlan, ConfigurationSequenceError>
		Build() {
			if (const auto validation = ValidateSnapshot(); !validation)
				return std::unexpected(validation.error());
			if (const auto validation = ValidatePolicy(); !validation)
				return std::unexpected(validation.error());

			auto wire_keys = BuildWireRegistryKeys();
			if (!wire_keys)
				return std::unexpected(wire_keys.error());

			auto initial_frames = BuildInitialFrames();
			if (!initial_frames)
				return std::unexpected(initial_frames.error());

			auto post_negotiation_frames = BuildPostNegotiationFrames(*wire_keys);
			if (!post_negotiation_frames)
				return std::unexpected(post_negotiation_frames.error());

			return ConfigurationTransmissionPlan{
				std::move(m_Snapshot),
				std::move(*initial_frames),
				std::move(*post_negotiation_frames),
				m_Policy.KnownPacks
			};
		}

	private:
		[[nodiscard]] RegistryGeneration Generation() const noexcept {
			return m_Snapshot == nullptr
				? Aurore::Util::NoRegistryGeneration
				: m_Snapshot->GetGeneration();
		}

		[[nodiscard]] ConfigurationSequenceError MakeError(
			ConfigurationSequenceErrorCode code,
			ConfigurationSequenceStage stage,
			std::optional<RegistryKind> registry = std::nullopt,
			std::optional<ResourceLocation> key = std::nullopt,
			ConfigurationSequenceErrorCause cause = {}) const {

			return ConfigurationSequenceError{
				.Code = code,
				.Stage = stage,
				.Generation = Generation(),
				.Registry = std::move(registry),
				.Key = std::move(key),
				.Pack = std::nullopt,
				.Index = std::nullopt,
				.ExistingIndex = std::nullopt,
				.Cause = std::move(cause),
			};
		}

		[[nodiscard]] std::expected<void, ConfigurationSequenceError>
		ValidateSnapshot() const {
			if (m_Snapshot == nullptr)
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::MissingSnapshot,
					ConfigurationSequenceStage::SnapshotValidation));

			if (m_Snapshot->GetGeneration() == Aurore::Util::NoRegistryGeneration) {
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::InvalidGeneration,
					ConfigurationSequenceStage::SnapshotValidation));
			}

			if (m_Snapshot->GetDimensionTypes().Empty())
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::MissingRequiredRegistryData,
					ConfigurationSequenceStage::SnapshotValidation,
					RegistryKind::DimensionType));

			if (m_Snapshot->GetBiomes().Empty())
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::MissingRequiredRegistryData,
					ConfigurationSequenceStage::SnapshotValidation,
					RegistryKind::Biome));

			if (const auto result = ValidateTagSet(
				m_Snapshot->GetBlocks(),
				m_Snapshot->GetBlockTags(),
				RegistryKind::Block);
				!result) {

				return result;
			}

			if (const auto result = ValidateTagSet(
				m_Snapshot->GetItems(),
				m_Snapshot->GetItemTags(),
				RegistryKind::Item);
				!result) {

				return result;
			}

			if (const auto result = ValidateTagSet(
				m_Snapshot->GetBiomes(),
				m_Snapshot->GetBiomeTags(),
				RegistryKind::Biome);
				!result) {

				return result;
			}

			return ValidateTagSet(
				m_Snapshot->GetDimensionTypes(),
				m_Snapshot->GetDimensionTypeTags(),
				RegistryKind::DimensionType);
		}

		template<typename TDefinition>
		[[nodiscard]] std::expected<void, ConfigurationSequenceError>
		ValidateTagSet(
			const Aurore::Util::Registry<TDefinition>& registry,
			const Aurore::Util::RegistryTagSet<TDefinition>& tags,
			RegistryKind registry_kind) const {

			if (tags.GetGeneration() != m_Snapshot->GetGeneration())
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::GenerationMismatch,
					ConfigurationSequenceStage::SnapshotValidation,
					registry_kind));

			for (const auto& tag : tags.GetTags()) {
				for (std::size_t member_index{ 0 };
					member_index < tag.Members.size();
					++member_index) {

					const auto runtime_id = tag.Members[member_index];
					if (static_cast<std::size_t>(runtime_id) < registry.Size())
						continue;

					auto error = MakeError(
						ConfigurationSequenceErrorCode::TagMemberOutOfRange,
						ConfigurationSequenceStage::SnapshotValidation,
						registry_kind,
						tag.Key);

					error.Index = member_index;
					return std::unexpected(std::move(error));
				}
			}

			return {};
		}

		[[nodiscard]] std::expected<void, ConfigurationSequenceError>
		ValidatePolicy() const {
			for (std::size_t index{ 0 }; index < m_Policy.EnabledFeatures.size(); ++index) {
				for (std::size_t existing_index{ 0 }; existing_index < index; ++existing_index) {
					if (m_Policy.EnabledFeatures[existing_index] != m_Policy.EnabledFeatures[index])
						continue;

					auto error = MakeError(
						ConfigurationSequenceErrorCode::DuplicateFeatureFlag,
						ConfigurationSequenceStage::PolicyValidation,
						std::nullopt,
						m_Policy.EnabledFeatures[index]);

					error.Index = index;
					error.ExistingIndex = existing_index;
					return std::unexpected(std::move(error));
				}
			}

			for (std::size_t index{ 0 }; index < m_Policy.KnownPacks.size(); ++index) {
				for (std::size_t existing_index{ 0 }; existing_index < index; ++existing_index) {
					if (m_Policy.KnownPacks[existing_index] != m_Policy.KnownPacks[index])
						continue;

					auto error = MakeError(
						ConfigurationSequenceErrorCode::DuplicateKnownPack,
						ConfigurationSequenceStage::PolicyValidation);

					error.Pack = m_Policy.KnownPacks[index];
					error.Index = index;
					error.ExistingIndex = existing_index;
					return std::unexpected(std::move(error));
				}
			}

			return {};
		}

		[[nodiscard]] std::expected<ResourceLocation, ConfigurationSequenceError>
		ParseWireRegistryKey(std::string_view value, RegistryKind registry) const {
			auto result = ResourceLocation::Parse(value);
			if (result)
				return std::move(*result);

			return std::unexpected(MakeError(
				ConfigurationSequenceErrorCode::InvalidProtocolIdentifier,
				ConfigurationSequenceStage::WireMapping,
				registry,
				std::nullopt,
				result.error()));
		}

		[[nodiscard]] std::expected<WireRegistryKeys, ConfigurationSequenceError>
		BuildWireRegistryKeys() const {
			auto blocks = ParseWireRegistryKey("minecraft:block", RegistryKind::Block);
			if (!blocks) return std::unexpected(blocks.error());

			auto items = ParseWireRegistryKey("minecraft:item", RegistryKind::Item);
			if (!items) return std::unexpected(items.error());

			auto biomes = ParseWireRegistryKey("minecraft:worldgen/biome", RegistryKind::Biome);
			if (!biomes) return std::unexpected(biomes.error());

			auto dimension_types = ParseWireRegistryKey("minecraft:dimension_type", RegistryKind::DimensionType);
			if (!dimension_types) return std::unexpected(dimension_types.error());

			return WireRegistryKeys{
				.Blocks = std::move(*blocks),
				.Items = std::move(*items),
				.Biomes = std::move(*biomes),
				.DimensionTypes = std::move(*dimension_types),
			};
		}

		template<typename TPacket>
		[[nodiscard]] std::expected<PacketFrame, ConfigurationSequenceError>
		EncodePacket(const TPacket& packet, ConfigurationSequenceStage stage,
			std::optional<RegistryKind> registry = std::nullopt) const {

			auto encoded = Configuration::Clientbound::Encode(packet, m_Limits);
			if (encoded)
				return std::move(*encoded);

			return std::unexpected(MakeError(
				ConfigurationSequenceErrorCode::PacketEncodingFailed,
				stage,
				registry,
				std::nullopt,
				encoded.error()));
		}

		[[nodiscard]] std::expected<PacketFrame, ConfigurationSequenceError>
		EncodeFinishConfiguration() const {
			const auto frame = Configuration::Clientbound::Encode(
				Configuration::Clientbound::FinishConfiguration{});

			const auto encoded_packet_id_size = EncodedVarIntSize(
				Configuration::Clientbound::FinishConfigurationPacketId);

			if (m_Limits.MaximumPacketDataBytes < encoded_packet_id_size) {
				return std::unexpected(MakeError(
					ConfigurationSequenceErrorCode::PacketEncodingFailed,
					ConfigurationSequenceStage::FinishConfiguration,
					std::nullopt,
					std::nullopt,
					Configuration::EncodeError{
						.Code = Configuration::EncodeErrorCode::PacketSizeExceeded,
						.ObservedValue = encoded_packet_id_size,
						.LimitValue = m_Limits.MaximumPacketDataBytes,
					}));
			}

			return frame;
		}

		[[nodiscard]] std::expected<std::vector<PacketFrame>, ConfigurationSequenceError>
		BuildInitialFrames() const {
			std::vector<PacketFrame> frames;
			frames.reserve(2);

			auto feature_flags = EncodePacket(
				Configuration::Clientbound::FeatureFlags{
					.Features = m_Policy.EnabledFeatures,
				},
				ConfigurationSequenceStage::FeatureFlags);

			if (!feature_flags)
				return std::unexpected(feature_flags.error());

			frames.push_back(std::move(*feature_flags));

			auto known_packs = EncodePacket(
				Configuration::Clientbound::SelectKnownPacks{
					.Packs = m_Policy.KnownPacks,
				},
				ConfigurationSequenceStage::KnownPacks);

			if (!known_packs)
				return std::unexpected(known_packs.error());

			frames.push_back(std::move(*known_packs));
			return frames;
		}

		template<typename TDefinition>
		[[nodiscard]] static Configuration::Clientbound::RegistryData
		BuildRegistryData(
			const ResourceLocation& wire_registry_key,
			const Aurore::Util::Registry<TDefinition>& registry) {

			Configuration::Clientbound::RegistryData result{
				.RegistryKey = wire_registry_key,
				.Entries = {},
			};

			result.Entries.reserve(registry.Size());

			for (const auto& entry : registry.GetEntries()) {
				result.Entries.push_back(
					Configuration::Clientbound::RegistryDataEntry{
						.Key = entry.Key,
						.Value = entry.Value.Data,
					});
			}

			return result;
		}

		template<typename TDefinition>
		[[nodiscard]] static Configuration::Clientbound::RegistryTagData
		BuildRegistryTags(
			const ResourceLocation& wire_registry_key,
			const Aurore::Util::RegistryTagSet<TDefinition>& tags) {

			Configuration::Clientbound::RegistryTagData result{
				.RegistryKey = wire_registry_key,
				.Tags = {},
			};

			result.Tags.reserve(tags.Size());

			for (const auto& tag : tags.GetTags()) {
				result.Tags.push_back(
					Configuration::Clientbound::TagData{
						.Key = tag.Key,
						.Members = tag.Members,
					});
			}

			return result;
		}

		[[nodiscard]] Configuration::Clientbound::UpdateTags
		BuildUpdateTags(const WireRegistryKeys& keys) const {
			Configuration::Clientbound::UpdateTags result;
			result.Registries.reserve(4);

			result.Registries.push_back(BuildRegistryTags(
				keys.Blocks,
				m_Snapshot->GetBlockTags()));

			result.Registries.push_back(BuildRegistryTags(
				keys.Items,
				m_Snapshot->GetItemTags()));

			result.Registries.push_back(BuildRegistryTags(
				keys.Biomes,
				m_Snapshot->GetBiomeTags()));

			result.Registries.push_back(BuildRegistryTags(
				keys.DimensionTypes,
				m_Snapshot->GetDimensionTypeTags()));

			return result;
		}

		[[nodiscard]] std::expected<std::vector<PacketFrame>, ConfigurationSequenceError>
		BuildPostNegotiationFrames(const WireRegistryKeys& keys) const {
			std::vector<PacketFrame> frames;
			frames.reserve(4);

			auto dimension_types = EncodePacket(
				BuildRegistryData(
					keys.DimensionTypes,
					m_Snapshot->GetDimensionTypes()),
				ConfigurationSequenceStage::DimensionTypeRegistry,
				RegistryKind::DimensionType);

			if (!dimension_types)
				return std::unexpected(dimension_types.error());

			frames.push_back(std::move(*dimension_types));

			auto biomes = EncodePacket(
				BuildRegistryData(
					keys.Biomes,
					m_Snapshot->GetBiomes()),
				ConfigurationSequenceStage::BiomeRegistry,
				RegistryKind::Biome);

			if (!biomes)
				return std::unexpected(biomes.error());

			frames.push_back(std::move(*biomes));

			auto tags = EncodePacket(
				BuildUpdateTags(keys),
				ConfigurationSequenceStage::Tags);

			if (!tags)
				return std::unexpected(tags.error());

			frames.push_back(std::move(*tags));

			auto finish = EncodeFinishConfiguration();
			if (!finish)
				return std::unexpected(finish.error());

			frames.push_back(std::move(*finish));
			return frames;
		}

		std::shared_ptr<const RegistrySnapshot> m_Snapshot;
		const ConfigurationSequencePolicy& m_Policy;
		const Configuration::Limits& m_Limits;
	};
}

namespace Aurore::Protocol {
	ConfigurationTransmissionPlan::ConfigurationTransmissionPlan(
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
		std::vector<PacketFrame> initial_frames,
		std::vector<PacketFrame> post_negotiation_frames,
		std::vector<Packets::Configuration::KnownPack> offered_known_packs) noexcept
		: m_Snapshot(std::move(snapshot)),
		m_InitialFrames(std::move(initial_frames)),
		m_PostNegotiationFrames(std::move(post_negotiation_frames)),
		m_OfferedKnownPacks(std::move(offered_known_packs)) {}

	Aurore::Util::RegistryGeneration
	ConfigurationTransmissionPlan::GetGeneration() const noexcept {
		return m_Snapshot == nullptr
			? Aurore::Util::NoRegistryGeneration
			: m_Snapshot->GetGeneration();
	}

	const std::shared_ptr<const Aurore::Util::RegistrySnapshot>&
	ConfigurationTransmissionPlan::GetSnapshot() const noexcept {
		return m_Snapshot;
	}

	std::span<const PacketFrame>
	ConfigurationTransmissionPlan::GetInitialFrames() const noexcept {
		return m_InitialFrames;
	}

	std::span<const PacketFrame>
	ConfigurationTransmissionPlan::GetPostNegotiationFrames() const noexcept {
		return m_PostNegotiationFrames;
	}

	std::span<const Packets::Configuration::KnownPack>
	ConfigurationTransmissionPlan::GetOfferedKnownPacks() const noexcept {
		return m_OfferedKnownPacks;
	}

	std::vector<PacketFrame>
	ConfigurationTransmissionPlan::ReleaseInitialFrames() noexcept {
		if (m_ConsumptionState != ConsumptionState::Ready)
			return {};

		m_ConsumptionState = ConsumptionState::InitialReleased;
		return std::move(m_InitialFrames);
	}

	std::vector<PacketFrame>
	ConfigurationTransmissionPlan::ReleasePostNegotiationFrames() noexcept {
		if (m_ConsumptionState != ConsumptionState::InitialReleased)
			return {};

		m_ConsumptionState = ConsumptionState::Complete;
		return std::move(m_PostNegotiationFrames);
	}

	std::expected<ConfigurationTransmissionPlan, ConfigurationSequenceError>
	ConfigurationSequenceBuilder::Build(
		std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
		const ConfigurationSequencePolicy& policy,
		const Packets::Configuration::Limits& limits) {

		return Detail::ConfigurationSequenceAssembler{
			std::move(snapshot),
			policy,
			limits
		}.Build();
	}
}

