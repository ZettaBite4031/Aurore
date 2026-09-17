#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>

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
	namespace Detail {
		class ConfigurationSequenceAssembler;
	}

	struct ConfigurationSequencePolicy final {
		std::vector<Aurore::Util::ResourceLocation> EnabledFeatures;
		std::vector<Packets::Configuration::KnownPack> KnownPacks;

		auto operator<=>(const ConfigurationSequencePolicy&) const noexcept = default;
	};

	enum class ConfigurationSequenceStage : std::uint8_t {
		SnapshotValidation,
		PolicyValidation,
		WireMapping,
		FeatureFlags,
		KnownPacks,
		DimensionTypeRegistry,
		BiomeRegistry,
		Tags,
		FinishConfiguration,
	};

	enum class ConfigurationSequenceErrorCode : std::uint8_t {
		MissingSnapshot,
		InvalidGeneration,
		GenerationMismatch,
		MissingRequiredRegistryData,
		TagMemberOutOfRange,
		DuplicateFeatureFlag,
		DuplicateKnownPack,
		InvalidProtocolIdentifier,
		PacketEncodingFailed,
	};

	using ConfigurationSequenceErrorCause = std::variant<
		std::monostate,
		Aurore::Util::ResourceLocationError,
		Packets::Configuration::EncodeError>;

	struct ConfigurationSequenceError final {
		ConfigurationSequenceErrorCode Code;
		ConfigurationSequenceStage Stage;

		Aurore::Util::RegistryGeneration Generation{
			Aurore::Util::NoRegistryGeneration
		};

		std::optional<Aurore::Util::RegistryKind> Registry{};
		std::optional<Aurore::Util::ResourceLocation> Key{};
		std::optional<Packets::Configuration::KnownPack> Pack{};

		std::optional<std::size_t> Index{};
		std::optional<std::size_t> ExistingIndex{};

		ConfigurationSequenceErrorCause Cause{};

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
		[[nodiscard]] std::span<const PacketFrame> GetPostNegotiationFrames() const noexcept;
		[[nodiscard]] std::span<const Packets::Configuration::KnownPack> GetOfferedKnownPacks() const noexcept;

		/*
			These methods retain their existing signatures for source
			compatibility, but now enforce the only valid consumption order:
			initial frames once, then post-negotiation frames once.
			Invalid consumption attempts return an empty vector.
		*/
		[[nodiscard]] std::vector<PacketFrame> ReleaseInitialFrames() noexcept;
		[[nodiscard]] std::vector<PacketFrame> ReleasePostNegotiationFrames() noexcept;

	private:
		enum class ConsumptionState : std::uint8_t {
			Ready,
			InitialReleased,
			Complete,
		};

		ConfigurationTransmissionPlan(
			std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
			std::vector<PacketFrame> initial_frames,
			std::vector<PacketFrame> post_negotiation_frames,
			std::vector<Packets::Configuration::KnownPack> offered_known_packs) noexcept;

		std::shared_ptr<const Aurore::Util::RegistrySnapshot> m_Snapshot;
		std::vector<PacketFrame> m_InitialFrames;
		std::vector<PacketFrame> m_PostNegotiationFrames;
		std::vector<Packets::Configuration::KnownPack> m_OfferedKnownPacks;
		ConsumptionState m_ConsumptionState{ ConsumptionState::Ready };

		friend class ConfigurationSequenceBuilder;
		friend class Detail::ConfigurationSequenceAssembler;
	};

	class ConfigurationSequenceBuilder final {
	public:
		[[nodiscard]] static std::expected<
			ConfigurationTransmissionPlan,
			ConfigurationSequenceError>
		Build(
			std::shared_ptr<const Aurore::Util::RegistrySnapshot> snapshot,
			const ConfigurationSequencePolicy& policy,
			const Packets::Configuration::Limits& limits =
				Packets::Configuration::DefaultLimits);
	};
}

