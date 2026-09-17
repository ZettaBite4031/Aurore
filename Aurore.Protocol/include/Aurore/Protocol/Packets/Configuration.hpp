#pragma once

#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/ProtocolTypes.hpp>

#include <Aurore/Util/Nbt.hpp>
#include <Aurore/Util/NbtBinary.hpp>
#include <Aurore/Util/Registry.hpp>
#include <Aurore/Util/ResourceLocation.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Aurore::Protocol::Packets::Configuration {
	inline constexpr std::int32_t TargetProtocolVersion{ 774 };

	enum class Direction : std::uint8_t {
		Clientbound,
		Serverbound,
	};

	struct PacketDescriptor final {
		std::int32_t Id;
		std::string_view Name;
		Direction PacketDirection;

		auto operator<=>(const PacketDescriptor&) const noexcept = default;
	};

	enum class ChatMode : std::int32_t {
		Enabled = 0,
		CommandsOnly = 1,
		Hidden = 2,
	};

	enum class MainHand : std::int32_t {
		Left = 0,
		Right = 1,
	};

	enum class ParticleStatus : std::int32_t {
		All = 0,
		Decreased = 1,
		Minimal = 2,
	};

	struct KnownPack final {
		std::string Namespace;
		std::string Id;
		std::string Version;

		auto operator<=>(const KnownPack&) const noexcept = default;
	};

	struct Limits final {
		std::size_t MaximumLocaleEncodedBytes{ 16 };

		std::size_t MaximumKnownPacks{ 64 };
		std::size_t MaximumKnownPackNamespaceEncodedBytes{ 64 };
		std::size_t MaximumKnownPackIdEncodedBytes{ 256 };
		std::size_t MaximumKnownPackVersionEncodedBytes{ 64 };

		std::size_t MaximumIdentifierEncodedBytes{ 32'767 };
		std::size_t MaximumFeatureFlags{ 1'024 };
		std::size_t MaximumRegistryEntries{ 1'048'576 };

		std::size_t MaximumTagRegistries{ 1'024 };
		std::size_t MaximumTagsPerRegistry{ 65'536 };
		std::size_t MaximumMembersPerTag{ 1'048'576 };
		std::size_t MaximumTotalTagMembers{ 4'194'304 };

		std::size_t MaximumCustomPayloadBytes{ 1'048'576 };

		std::size_t MaximumClientBrandEncodedBytes{ 256 };

		std::size_t MaximumPacketDataBytes{
			PacketStreamDecoder::DefaultMaximumPacketSize
		};

		Aurore::Util::NbtLimits RegistryEntryNbt{};
	};

	inline constexpr Limits DefaultLimits{};

	enum class EncodeErrorCode : std::uint8_t {
		KnownPackLimitExceeded,
		KnownPackNamespaceLimitExceeded,
		KnownPackIdLimitExceeded,
		KnownPackVersionLimitExceeded,

		FeatureFlagLimitExceeded,
		IdentifierLimitExceeded,
		RegistryEntryLimitExceeded,
		RegistryEntryNbtEncodingFailed,
		TagRegistryLimitExceeded,
		TagLimitExceeded,
		TagMemberLimitExceeded,
		TotalTagMemberLimitExceeded,
		RuntimeIdEncodingOverflow,
		PacketSizeExceeded,
	};

	struct EncodeError final {
		EncodeErrorCode Code;

		std::optional<std::size_t> PackIndex{};
		std::optional<std::size_t> FeatureIndex{};
		std::optional<std::size_t> RegistryIndex{};
		std::optional<std::size_t> EntryIndex{};
		std::optional<std::size_t> TagIndex{};
		std::optional<std::size_t> MemberIndex{};

		std::optional<Aurore::Util::ResourceLocation> RegistryKey{};
		std::optional<Aurore::Util::ResourceLocation> EntryKey{};
		std::optional<Aurore::Util::ResourceLocation> TagKey{};
		std::optional<Aurore::Util::ResourceLocation> Identifier{};

		std::optional<Aurore::Util::NbtWriteError> NbtError{};

		std::size_t ObservedValue{ 0 };
		std::size_t LimitValue{ 0 };

		auto operator<=>(const EncodeError&) const noexcept = default;
	};

	template<typename T>
	using EncodeResult = std::expected<T, EncodeError>;

	enum class ContractErrorCode : std::uint8_t {
		DuplicateSelectedPack,
		UnofferedPack,
	};

	struct ContractError final {
		ContractErrorCode Code;
		KnownPack Pack;
		std::size_t SelectedIndex{ 0 };
		std::optional<std::size_t> ExistingIndex{};

		auto operator<=>(const ContractError&) const noexcept = default;
	};

	enum class SequenceStep : std::uint8_t {
		SendFeatureFlags,
		SendSelectKnownPacks,
		ReceiveSelectKnownPacks,
		SendRegistryData,
		SendTags,
		SendFinishConfiguration,
		ReceiveFinishConfiguration,
	};

	/*
		Client Information is intentionally not represented in this linear
		sequence. The vanilla client may send it immediately after entering
		Configuration, independently of the server's known-pack negotiation.
	*/
	inline constexpr std::array SyntheticConfigurationOrder{
		SequenceStep::SendFeatureFlags,
		SequenceStep::SendSelectKnownPacks,
		SequenceStep::ReceiveSelectKnownPacks,
		SequenceStep::SendRegistryData,
		SequenceStep::SendTags,
		SequenceStep::SendFinishConfiguration,
		SequenceStep::ReceiveFinishConfiguration,
	};

	[[nodiscard]] std::expected<void, ContractError> ValidateKnownPackSelection(
		std::span<const KnownPack> offered,
		std::span<const KnownPack> selected);

	namespace Clientbound {
		inline constexpr std::int32_t CookieRequestPacketId{ 0x00 };
		inline constexpr std::int32_t CustomPayloadPacketId{ 0x01 };
		inline constexpr std::int32_t DisconnectPacketId{ 0x02 };
		inline constexpr std::int32_t FinishConfigurationPacketId{ 0x03 };
		inline constexpr std::int32_t KeepAlivePacketId{ 0x04 };
		inline constexpr std::int32_t PingPacketId{ 0x05 };
		inline constexpr std::int32_t ResetChatPacketId{ 0x06 };
		inline constexpr std::int32_t RegistryDataPacketId{ 0x07 };
		inline constexpr std::int32_t RemoveResourcePackPacketId{ 0x08 };
		inline constexpr std::int32_t AddResourcePackPacketId{ 0x09 };
		inline constexpr std::int32_t StoreCookiePacketId{ 0x0A };
		inline constexpr std::int32_t TransferPacketId{ 0x0B };
		inline constexpr std::int32_t FeatureFlagsPacketId{ 0x0C };
		inline constexpr std::int32_t TagsPacketId{ 0x0D };
		inline constexpr std::int32_t SelectKnownPacksPacketId{ 0x0E };
		inline constexpr std::int32_t CustomReportDetailsPacketId{ 0x0F };
		inline constexpr std::int32_t ServerLinksPacketId{ 0x10 };
		inline constexpr std::int32_t ClearDialogPacketId{ 0x11 };
		inline constexpr std::int32_t ShowDialogPacketId{ 0x12 };
		inline constexpr std::int32_t CodeOfConductPacketId{ 0x13 };

		inline constexpr std::array PacketDescriptors{
			PacketDescriptor{ CookieRequestPacketId, "cookie_request", Direction::Clientbound },
			PacketDescriptor{ CustomPayloadPacketId, "custom_payload", Direction::Clientbound },
			PacketDescriptor{ DisconnectPacketId, "disconnect", Direction::Clientbound },
			PacketDescriptor{ FinishConfigurationPacketId, "finish_configuration", Direction::Clientbound },
			PacketDescriptor{ KeepAlivePacketId, "keep_alive", Direction::Clientbound },
			PacketDescriptor{ PingPacketId, "ping", Direction::Clientbound },
			PacketDescriptor{ ResetChatPacketId, "reset_chat", Direction::Clientbound },
			PacketDescriptor{ RegistryDataPacketId, "registry_data", Direction::Clientbound },
			PacketDescriptor{ RemoveResourcePackPacketId, "remove_resource_pack", Direction::Clientbound },
			PacketDescriptor{ AddResourcePackPacketId, "add_resource_pack", Direction::Clientbound },
			PacketDescriptor{ StoreCookiePacketId, "store_cookie", Direction::Clientbound },
			PacketDescriptor{ TransferPacketId, "transfer", Direction::Clientbound },
			PacketDescriptor{ FeatureFlagsPacketId, "feature_flags", Direction::Clientbound },
			PacketDescriptor{ TagsPacketId, "tags", Direction::Clientbound },
			PacketDescriptor{ SelectKnownPacksPacketId, "select_known_packs", Direction::Clientbound },
			PacketDescriptor{ CustomReportDetailsPacketId, "custom_report_details", Direction::Clientbound },
			PacketDescriptor{ ServerLinksPacketId, "server_links", Direction::Clientbound },
			PacketDescriptor{ ClearDialogPacketId, "clear_dialog", Direction::Clientbound },
			PacketDescriptor{ ShowDialogPacketId, "show_dialog", Direction::Clientbound },
			PacketDescriptor{ CodeOfConductPacketId, "code_of_conduct", Direction::Clientbound },
		};

		struct FeatureFlags final {
			std::vector<Aurore::Util::ResourceLocation> Features;

			auto operator<=>(const FeatureFlags&) const noexcept = default;
		};

		struct RegistryDataEntry final {
			Aurore::Util::ResourceLocation Key;
			std::optional<Aurore::Util::NbtCompound> Value;

			bool operator==(const RegistryDataEntry&) const noexcept = default;
		};

		struct RegistryData final {
			Aurore::Util::ResourceLocation RegistryKey;
			std::vector<RegistryDataEntry> Entries;

			bool operator==(const RegistryData&) const noexcept = default;
		};

		struct TagData final {
			Aurore::Util::ResourceLocation Key;
			std::vector<Aurore::Util::RegistryRuntimeId> Members;

			auto operator<=>(const TagData&) const noexcept = default;
		};

		struct RegistryTagData final {
			Aurore::Util::ResourceLocation RegistryKey;
			std::vector<TagData> Tags;

			auto operator<=>(const RegistryTagData&) const noexcept = default;
		};

		struct UpdateTags final {
			std::vector<RegistryTagData> Registries;

			auto operator<=>(const UpdateTags&) const noexcept = default;
		};

		struct SelectKnownPacks final {
			std::vector<KnownPack> Packs;

			auto operator<=>(const SelectKnownPacks&) const noexcept = default;
		};

		struct FinishConfiguration final {
			auto operator<=>(const FinishConfiguration&) const noexcept = default;
		};

		using Packet = std::variant<
			FeatureFlags,
			RegistryData,
			UpdateTags,
			SelectKnownPacks,
			FinishConfiguration>;

		[[nodiscard]] EncodeResult<PacketFrame> Encode(
			const FeatureFlags& packet,
			const Limits& limits = DefaultLimits);

		[[nodiscard]] EncodeResult<PacketFrame> Encode(
			const RegistryData& packet,
			const Limits& limits = DefaultLimits);

		[[nodiscard]] EncodeResult<PacketFrame> Encode(
			const UpdateTags& packet,
			const Limits& limits = DefaultLimits);

		[[nodiscard]] EncodeResult<PacketFrame> Encode(
			const SelectKnownPacks& packet,
			const Limits& limits = DefaultLimits);

		[[nodiscard]] PacketFrame Encode(
			const FinishConfiguration& packet);

		[[nodiscard]] EncodeResult<PacketFrame> Encode(
			const Packet& packet,
			const Limits& limits = DefaultLimits);
	}

	namespace Serverbound {
		inline constexpr std::int32_t ClientInformationPacketId{ 0x00 };
		inline constexpr std::int32_t CookieResponsePacketId{ 0x01 };
		inline constexpr std::int32_t CustomPayloadPacketId{ 0x02 };
		inline constexpr std::int32_t FinishConfigurationPacketId{ 0x03 };
		inline constexpr std::int32_t KeepAlivePacketId{ 0x04 };
		inline constexpr std::int32_t PongPacketId{ 0x05 };
		inline constexpr std::int32_t ResourcePackResponsePacketId{ 0x06 };
		inline constexpr std::int32_t SelectKnownPacksPacketId{ 0x07 };
		inline constexpr std::int32_t CustomClickActionPacketId{ 0x08 };
		inline constexpr std::int32_t AcceptCodeOfConductPacketId{ 0x09 };

		inline constexpr std::array PacketDescriptors{
			PacketDescriptor{ ClientInformationPacketId, "client_information", Direction::Serverbound },
			PacketDescriptor{ CookieResponsePacketId, "cookie_response", Direction::Serverbound },
			PacketDescriptor{ CustomPayloadPacketId, "custom_payload", Direction::Serverbound },
			PacketDescriptor{ FinishConfigurationPacketId, "finish_configuration", Direction::Serverbound },
			PacketDescriptor{ KeepAlivePacketId, "keep_alive", Direction::Serverbound },
			PacketDescriptor{ PongPacketId, "pong", Direction::Serverbound },
			PacketDescriptor{ ResourcePackResponsePacketId, "resource_pack_response", Direction::Serverbound },
			PacketDescriptor{ SelectKnownPacksPacketId, "select_known_packs", Direction::Serverbound },
			PacketDescriptor{ CustomClickActionPacketId, "custom_click_action", Direction::Serverbound },
			PacketDescriptor{ AcceptCodeOfConductPacketId, "accept_code_of_conduct", Direction::Serverbound },
		};

		struct ClientInformation final {
			std::string Locale;
			std::int8_t ViewDistance{ 0 };
			ChatMode ChatModeValue{ ChatMode::Enabled };
			bool ChatColors{ true };
			std::uint8_t SkinParts{ 0 };
			MainHand MainHandValue{ MainHand::Right };
			bool EnableTextFiltering{ false };
			bool EnableServerListing{ true };
			ParticleStatus ParticleStatusValue{ ParticleStatus::All };

			auto operator<=>(const ClientInformation&) const noexcept = default;
		};

		struct ClientBrand final {
			std::string Brand;

			auto operator<=>(const ClientBrand&) const noexcept = default;
		};

		struct CustomPayload final {
			Aurore::Util::ResourceLocation Channel;
			std::vector<std::byte> Data;

			auto operator<=>(const CustomPayload&) const noexcept = default;
		};

		struct SelectKnownPacks final {
			std::vector<KnownPack> Packs;

			auto operator<=>(const SelectKnownPacks&) const noexcept = default;
		};

		struct FinishConfiguration final {
			auto operator<=>(const FinishConfiguration&) const noexcept = default;
		};

		using Packet = std::variant<
			ClientInformation,
			ClientBrand,
			CustomPayload,
			SelectKnownPacks,
			FinishConfiguration>;

		/*
			Configuration control packets and common custom payloads are decoded here.

			The minecraft:brand channel is promoted to ClientBrand so session logic
			does not need to reinterpret arbitrary byte buffers. Unknown channels are
			retained as bounded CustomPayload values and are currently ignored by the
			session without affecting Configuration sequencing.
		*/
		[[nodiscard]] ProtocolResult<Packet> Decode(
			const PacketFrame& frame,
			const Limits& limits = DefaultLimits);
	}
}
