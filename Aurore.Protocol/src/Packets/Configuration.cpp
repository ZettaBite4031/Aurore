#include <Aurore/Protocol/Packets/Configuration.hpp>

#include <Aurore/Util/ByteBuffer.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace Aurore::Protocol::Packets::Configuration {
	namespace {
		struct EncodedRegistryEntry final {
			std::string Key;
			std::optional<std::vector<std::byte>> Value;
		};

		struct EncodedTagRegistry final {
			std::string Key;
			std::vector<std::string> TagKeys;
		};

		struct IdentifierErrorContext final {
			std::optional<std::size_t> FeatureIndex{};
			std::optional<std::size_t> RegistryIndex{};
			std::optional<std::size_t> EntryIndex{};
			std::optional<std::size_t> TagIndex{};

			std::optional<Aurore::Util::ResourceLocation> RegistryKey{};
			std::optional<Aurore::Util::ResourceLocation> EntryKey{};
			std::optional<Aurore::Util::ResourceLocation> TagKey{};
		};

		[[nodiscard]] constexpr std::size_t SaturatingAdd(
			std::size_t left,
			std::size_t right) noexcept {

			if (right > std::numeric_limits<std::size_t>::max() - left)
				return std::numeric_limits<std::size_t>::max();

			return left + right;
		}

		[[nodiscard]] constexpr std::size_t EncodedVarIntSize(
			std::int32_t value) noexcept {

			std::uint32_t remaining = static_cast<std::uint32_t>(value);
			std::size_t result{ 1 };

			while ((remaining & ~0x7Fu) != 0) {
				remaining >>= 7;
				++result;
			}

			return result;
		}

		[[nodiscard]] EncodeResult<PacketFrame> MakeFrame(
			std::int32_t packet_id,
			const Aurore::Util::ByteBuffer& payload,
			const Limits& limits) {

			const auto packet_id_bytes = EncodedVarIntSize(packet_id);

			if (payload.Size() > limits.MaximumPacketDataBytes
				|| packet_id_bytes
					> limits.MaximumPacketDataBytes - payload.Size()) {

				return std::unexpected(EncodeError{
					.Code = EncodeErrorCode::PacketSizeExceeded,
					.ObservedValue = SaturatingAdd(payload.Size(), packet_id_bytes),
					.LimitValue = limits.MaximumPacketDataBytes,
					});
			}

			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end()),
			};
		}

		template<typename TEnum>
		[[nodiscard]] std::optional<TEnum> ParseEnum(
			std::int32_t value,
			TEnum minimum,
			TEnum maximum) noexcept {

			const auto minimum_value = static_cast<std::int32_t>(minimum);
			const auto maximum_value = static_cast<std::int32_t>(maximum);

			if (value < minimum_value || value > maximum_value)
				return std::nullopt;

			return static_cast<TEnum>(value);
		}

		[[nodiscard]] std::optional<std::int8_t> ReadInt8(
			Aurore::Util::ByteReader& reader) noexcept {

			const auto value = reader.ReadUnsignedByte();
			if (!value) return std::nullopt;

			return std::bit_cast<std::int8_t>(*value);
		}

		[[nodiscard]] ProtocolResult<std::vector<KnownPack>> ReadKnownPacks(
			Aurore::Util::ByteReader& reader,
			const Limits& limits) {

			const auto count = reader.ReadVarInt();
			if (!count || *count < 0)
				return std::unexpected(ProtocolError::MalformedPacket);

			const auto count_value = static_cast<std::size_t>(*count);
			if (count_value > limits.MaximumKnownPacks)
				return std::unexpected(ProtocolError::MalformedPacket);

			std::vector<KnownPack> packs;
			packs.reserve(count_value);

			for (std::size_t index{ 0 }; index < count_value; ++index) {
				auto namespace_name = reader.ReadString(
					limits.MaximumKnownPackNamespaceEncodedBytes);

				auto id = reader.ReadString(
					limits.MaximumKnownPackIdEncodedBytes);

				auto version = reader.ReadString(
					limits.MaximumKnownPackVersionEncodedBytes);

				if (!namespace_name || !id || !version)
					return std::unexpected(ProtocolError::MalformedPacket);

				packs.push_back(KnownPack{
					.Namespace = std::move(*namespace_name),
					.Id = std::move(*id),
					.Version = std::move(*version),
					});
			}

			return packs;
		}

		[[nodiscard]] bool IsClientBrandChannel(const Aurore::Util::ResourceLocation& channel) noexcept {
			return channel.GetNamespace() == "minecraft" && channel.GetPath() == "brand";
		}

		[[nodiscard]] std::optional<EncodeError> ValidateKnownPacksForEncoding(
			std::span<const KnownPack> packs,
			const Limits& limits) noexcept {

			if (packs.size() > limits.MaximumKnownPacks
				|| packs.size() > static_cast<std::size_t>(
					std::numeric_limits<std::int32_t>::max())) {

				return EncodeError{
					.Code = EncodeErrorCode::KnownPackLimitExceeded,
					.ObservedValue = packs.size(),
					.LimitValue = std::min(
						limits.MaximumKnownPacks,
						static_cast<std::size_t>(
							std::numeric_limits<std::int32_t>::max())),
				};
			}

			for (std::size_t index{ 0 }; index < packs.size(); ++index) {
				const auto& pack = packs[index];

				if (pack.Namespace.size()
			> limits.MaximumKnownPackNamespaceEncodedBytes) {

					return EncodeError{
						.Code = EncodeErrorCode::KnownPackNamespaceLimitExceeded,
						.PackIndex = index,
						.ObservedValue = pack.Namespace.size(),
						.LimitValue = limits.MaximumKnownPackNamespaceEncodedBytes,
					};
				}

				if (pack.Id.size()
					> limits.MaximumKnownPackIdEncodedBytes) {

					return EncodeError{
						.Code = EncodeErrorCode::KnownPackIdLimitExceeded,
						.PackIndex = index,
						.ObservedValue = pack.Id.size(),
						.LimitValue = limits.MaximumKnownPackIdEncodedBytes,
					};
				}

				if (pack.Version.size()
					> limits.MaximumKnownPackVersionEncodedBytes) {

					return EncodeError{
						.Code = EncodeErrorCode::KnownPackVersionLimitExceeded,
						.PackIndex = index,
						.ObservedValue = pack.Version.size(),
						.LimitValue = limits.MaximumKnownPackVersionEncodedBytes,
					};
				}
			}

			return std::nullopt;
		}

		[[nodiscard]] bool ExceedsVarIntCount(
			std::size_t count,
			std::size_t configured_limit) noexcept {

			return count > configured_limit
				|| count > static_cast<std::size_t>(
					std::numeric_limits<std::int32_t>::max());
		}

		[[nodiscard]] std::size_t EffectiveVarIntCountLimit(
			std::size_t configured_limit) noexcept {

			return std::min(
				configured_limit,
				static_cast<std::size_t>(
					std::numeric_limits<std::int32_t>::max()));
		}

		[[nodiscard]] std::optional<EncodeError> ValidateIdentifier(
			const Aurore::Util::ResourceLocation& identifier,
			const Limits& limits,
			IdentifierErrorContext context = {}) {

			const auto encoded = identifier.ToString();
			if (encoded.size() <= limits.MaximumIdentifierEncodedBytes)
				return std::nullopt;

			return EncodeError{
				.Code = EncodeErrorCode::IdentifierLimitExceeded,
				.FeatureIndex = context.FeatureIndex,
				.RegistryIndex = context.RegistryIndex,
				.EntryIndex = context.EntryIndex,
				.TagIndex = context.TagIndex,
				.RegistryKey = std::move(context.RegistryKey),
				.EntryKey = std::move(context.EntryKey),
				.TagKey = std::move(context.TagKey),
				.Identifier = identifier,
				.ObservedValue = encoded.size(),
				.LimitValue = limits.MaximumIdentifierEncodedBytes,
			};
		}

	}

	std::expected<void, ContractError> ValidateKnownPackSelection(
		std::span<const KnownPack> offered,
		std::span<const KnownPack> selected) {

		for (std::size_t selected_index{ 0 };
			selected_index < selected.size();
			++selected_index) {

			const auto& selected_pack = selected[selected_index];

			for (std::size_t existing_index{ 0 };
				existing_index < selected_index;
				++existing_index) {

				if (selected[existing_index] != selected_pack)
					continue;

				return std::unexpected(ContractError{
					.Code = ContractErrorCode::DuplicateSelectedPack,
					.Pack = selected_pack,
					.SelectedIndex = selected_index,
					.ExistingIndex = existing_index,
					});
			}

			const auto offered_iterator = std::find(
				offered.begin(),
				offered.end(),
				selected_pack);

			if (offered_iterator != offered.end())
				continue;

			return std::unexpected(ContractError{
				.Code = ContractErrorCode::UnofferedPack,
				.Pack = selected_pack,
				.SelectedIndex = selected_index,
				.ExistingIndex = std::nullopt,
				});
		}

		return {};
	}

	namespace Clientbound {
		EncodeResult<PacketFrame> Encode(
			const FeatureFlags& packet,
			const Limits& limits) {

			if (ExceedsVarIntCount(
				packet.Features.size(),
				limits.MaximumFeatureFlags)) {

				return std::unexpected(EncodeError{
					.Code = EncodeErrorCode::FeatureFlagLimitExceeded,
					.ObservedValue = packet.Features.size(),
					.LimitValue = EffectiveVarIntCountLimit(
						limits.MaximumFeatureFlags),
					});
			}

			std::vector<std::string> encoded_features;
			encoded_features.reserve(packet.Features.size());

			for (std::size_t index{ 0 };
				index < packet.Features.size();
				++index) {

				const auto& feature = packet.Features[index];

				if (const auto error = ValidateIdentifier(
					feature,
					limits,
					IdentifierErrorContext{
						.FeatureIndex = index,
					});
					error.has_value()) {

					return std::unexpected(*error);
				}

				encoded_features.push_back(feature.ToString());
			}

			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(
				static_cast<std::int32_t>(packet.Features.size()));

			for (const auto& feature : encoded_features)
				payload.WriteString(feature);

			return MakeFrame(FeatureFlagsPacketId, payload, limits);
		}

		EncodeResult<PacketFrame> Encode(
			const RegistryData& packet,
			const Limits& limits) {

			if (const auto error = ValidateIdentifier(
				packet.RegistryKey,
				limits,
				IdentifierErrorContext{
					.RegistryKey = packet.RegistryKey,
				});
				error.has_value()) {

				return std::unexpected(*error);
			}

			if (ExceedsVarIntCount(
				packet.Entries.size(),
				limits.MaximumRegistryEntries)) {

				return std::unexpected(EncodeError{
					.Code = EncodeErrorCode::RegistryEntryLimitExceeded,
					.RegistryKey = packet.RegistryKey,
					.ObservedValue = packet.Entries.size(),
					.LimitValue = EffectiveVarIntCountLimit(
						limits.MaximumRegistryEntries),
					});
			}

			std::vector<EncodedRegistryEntry> encoded_entries;
			encoded_entries.reserve(packet.Entries.size());

			for (std::size_t index{ 0 };
				index < packet.Entries.size();
				++index) {

				const auto& entry = packet.Entries[index];

				if (const auto error = ValidateIdentifier(
					entry.Key,
					limits,
					IdentifierErrorContext{
						.EntryIndex = index,
						.RegistryKey = packet.RegistryKey,
						.EntryKey = entry.Key,
					});
					error.has_value()) {

					return std::unexpected(*error);
				}

				EncodedRegistryEntry encoded_entry{
					.Key = entry.Key.ToString(),
					.Value = std::nullopt,
				};

				if (entry.Value) {
					auto encoded_nbt =
						Aurore::Util::NbtWriter::WriteNetworkCompound(
							*entry.Value,
							limits.RegistryEntryNbt);

					if (!encoded_nbt) {
						return std::unexpected(EncodeError{
							.Code = EncodeErrorCode::RegistryEntryNbtEncodingFailed,
							.EntryIndex = index,
							.RegistryKey = packet.RegistryKey,
							.EntryKey = entry.Key,
							.NbtError = encoded_nbt.error(),
							});
					}

					encoded_entry.Value = std::move(*encoded_nbt);
				}

				encoded_entries.push_back(std::move(encoded_entry));
			}

			Aurore::Util::ByteBuffer payload;
			payload.WriteString(packet.RegistryKey.ToString());
			payload.WriteVarInt(
				static_cast<std::int32_t>(encoded_entries.size()));

			for (const auto& entry : encoded_entries) {
				payload.WriteString(entry.Key);
				payload.WriteBool(entry.Value.has_value());

				if (entry.Value)
					payload.WriteBytes(*entry.Value);
			}

			return MakeFrame(RegistryDataPacketId, payload, limits);
		}

		EncodeResult<PacketFrame> Encode(
			const UpdateTags& packet,
			const Limits& limits) {

			if (ExceedsVarIntCount(
				packet.Registries.size(),
				limits.MaximumTagRegistries)) {

				return std::unexpected(EncodeError{
					.Code = EncodeErrorCode::TagRegistryLimitExceeded,
					.ObservedValue = packet.Registries.size(),
					.LimitValue = EffectiveVarIntCountLimit(
						limits.MaximumTagRegistries),
					});
			}

			std::size_t total_members{ 0 };
			std::vector<EncodedTagRegistry> encoded_registries;
			encoded_registries.reserve(packet.Registries.size());

			for (std::size_t registry_index{ 0 };
				registry_index < packet.Registries.size();
				++registry_index) {

				const auto& registry = packet.Registries[registry_index];

				if (const auto error = ValidateIdentifier(
					registry.RegistryKey,
					limits,
					IdentifierErrorContext{
						.RegistryIndex = registry_index,
						.RegistryKey = registry.RegistryKey,
					});
					error.has_value()) {

					return std::unexpected(*error);
				}

				if (ExceedsVarIntCount(
					registry.Tags.size(),
					limits.MaximumTagsPerRegistry)) {

					return std::unexpected(EncodeError{
						.Code = EncodeErrorCode::TagLimitExceeded,
						.RegistryIndex = registry_index,
						.RegistryKey = registry.RegistryKey,
						.ObservedValue = registry.Tags.size(),
						.LimitValue = EffectiveVarIntCountLimit(
							limits.MaximumTagsPerRegistry),
						});
				}

				EncodedTagRegistry encoded_registry{
					.Key = registry.RegistryKey.ToString(),
					.TagKeys = {},
				};
				encoded_registry.TagKeys.reserve(registry.Tags.size());

				for (std::size_t tag_index{ 0 };
					tag_index < registry.Tags.size();
					++tag_index) {

					const auto& tag = registry.Tags[tag_index];

					if (const auto error = ValidateIdentifier(
						tag.Key,
						limits,
						IdentifierErrorContext{
							.RegistryIndex = registry_index,
							.TagIndex = tag_index,
							.RegistryKey = registry.RegistryKey,
							.TagKey = tag.Key,
						});
						error.has_value()) {

						return std::unexpected(*error);
					}

					if (ExceedsVarIntCount(
						tag.Members.size(),
						limits.MaximumMembersPerTag)) {

						return std::unexpected(EncodeError{
							.Code = EncodeErrorCode::TagMemberLimitExceeded,
							.RegistryIndex = registry_index,
							.TagIndex = tag_index,
							.RegistryKey = registry.RegistryKey,
							.TagKey = tag.Key,
							.ObservedValue = tag.Members.size(),
							.LimitValue = EffectiveVarIntCountLimit(
								limits.MaximumMembersPerTag),
							});
					}

					if (total_members > limits.MaximumTotalTagMembers
						|| tag.Members.size()
				> limits.MaximumTotalTagMembers - total_members) {

						return std::unexpected(EncodeError{
							.Code = EncodeErrorCode::TotalTagMemberLimitExceeded,
							.RegistryIndex = registry_index,
							.TagIndex = tag_index,
							.RegistryKey = registry.RegistryKey,
							.TagKey = tag.Key,
							.ObservedValue = SaturatingAdd(total_members, tag.Members.size()),
							.LimitValue = limits.MaximumTotalTagMembers,
							});
					}

					for (std::size_t member_index{ 0 };
						member_index < tag.Members.size();
						++member_index) {

						const auto runtime_id = tag.Members[member_index];

						if (runtime_id <= static_cast<Aurore::Util::RegistryRuntimeId>(
							std::numeric_limits<std::int32_t>::max())) {
							continue;
						}

						return std::unexpected(EncodeError{
							.Code = EncodeErrorCode::RuntimeIdEncodingOverflow,
							.RegistryIndex = registry_index,
							.TagIndex = tag_index,
							.MemberIndex = member_index,
							.RegistryKey = registry.RegistryKey,
							.TagKey = tag.Key,
							.ObservedValue = static_cast<std::size_t>(runtime_id),
							.LimitValue = static_cast<std::size_t>(
								std::numeric_limits<std::int32_t>::max()),
							});
					}

					total_members += tag.Members.size();
					encoded_registry.TagKeys.push_back(tag.Key.ToString());
				}

				encoded_registries.push_back(std::move(encoded_registry));
			}

			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(
				static_cast<std::int32_t>(packet.Registries.size()));

			for (std::size_t registry_index{ 0 };
				registry_index < packet.Registries.size();
				++registry_index) {

				const auto& registry = packet.Registries[registry_index];
				const auto& encoded_registry = encoded_registries[registry_index];

				payload.WriteString(encoded_registry.Key);
				payload.WriteVarInt(
					static_cast<std::int32_t>(registry.Tags.size()));

				for (std::size_t tag_index{ 0 };
					tag_index < registry.Tags.size();
					++tag_index) {

					const auto& tag = registry.Tags[tag_index];
					payload.WriteString(encoded_registry.TagKeys[tag_index]);
					payload.WriteVarInt(
						static_cast<std::int32_t>(tag.Members.size()));

					for (const auto runtime_id : tag.Members) {
						payload.WriteVarInt(
							static_cast<std::int32_t>(runtime_id));
					}
				}
			}

			return MakeFrame(TagsPacketId, payload, limits);
		}

		EncodeResult<PacketFrame> Encode(
			const SelectKnownPacks& packet,
			const Limits& limits) {

			if (const auto error =
				ValidateKnownPacksForEncoding(packet.Packs, limits);
				error.has_value()) {

				return std::unexpected(*error);
			}

			Aurore::Util::ByteBuffer payload;
			payload.WriteVarInt(
				static_cast<std::int32_t>(packet.Packs.size()));

			for (const auto& pack : packet.Packs) {
				payload.WriteString(pack.Namespace);
				payload.WriteString(pack.Id);
				payload.WriteString(pack.Version);
			}

			return MakeFrame(SelectKnownPacksPacketId, payload, limits);
		}

		PacketFrame Encode(const FinishConfiguration& packet) {
			(void)packet;

			return PacketFrame{
				.PacketId = FinishConfigurationPacketId,
				.Payload = {},
			};
		}

		EncodeResult<PacketFrame> Encode(
			const Packet& packet,
			const Limits& limits) {

			return std::visit(
				[&](const auto& value) -> EncodeResult<PacketFrame> {
					using TValue = std::remove_cvref_t<decltype(value)>;

					if constexpr (std::is_same_v<
						TValue,
						FinishConfiguration>) {

						return Encode(value);
					}
					else {
						return Encode(value, limits);
					}
				},
				packet);
		}
	}

	namespace Serverbound {
		ProtocolResult<Packet> Decode(
			const PacketFrame& frame,
			const Limits& limits) {

			switch (frame.PacketId) {
			case ClientInformationPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);

				auto locale = reader.ReadString(
					limits.MaximumLocaleEncodedBytes);

				const auto view_distance = ReadInt8(reader);
				const auto chat_mode_value = reader.ReadVarInt();
				const auto chat_colors = reader.ReadBool();
				const auto skin_parts = reader.ReadUnsignedByte();
				const auto main_hand_value = reader.ReadVarInt();
				const auto enable_text_filtering = reader.ReadBool();
				const auto enable_server_listing = reader.ReadBool();
				const auto particle_status_value = reader.ReadVarInt();

				if (!locale
					|| !view_distance
					|| !chat_mode_value
					|| !chat_colors
					|| !skin_parts
					|| !main_hand_value
					|| !enable_text_filtering
					|| !enable_server_listing
					|| !particle_status_value) {

					return std::unexpected(
						ProtocolError::MalformedPacket);
				}

				const auto chat_mode = ParseEnum(
					*chat_mode_value,
					ChatMode::Enabled,
					ChatMode::Hidden);

				const auto main_hand = ParseEnum(
					*main_hand_value,
					MainHand::Left,
					MainHand::Right);

				const auto particle_status = ParseEnum(
					*particle_status_value,
					ParticleStatus::All,
					ParticleStatus::Minimal);

				if (!chat_mode || !main_hand || !particle_status)
					return std::unexpected(
						ProtocolError::MalformedPacket);

				if (!reader.Empty())
					return std::unexpected(
						ProtocolError::TrailingPacketData);

				return Packet{
					ClientInformation{
						.Locale = std::move(*locale),
						.ViewDistance = *view_distance,
						.ChatModeValue = *chat_mode,
						.ChatColors = *chat_colors,
						.SkinParts = *skin_parts,
						.MainHandValue = *main_hand,
						.EnableTextFiltering =
							*enable_text_filtering,
						.EnableServerListing =
							*enable_server_listing,
						.ParticleStatusValue =
							*particle_status,
					}
				};
			}

			case CustomPayloadPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);
				auto channel_text = reader.ReadString(limits.MaximumIdentifierEncodedBytes);
				if (!channel_text)
					return std::unexpected(ProtocolError::MalformedPacket);

				auto channel = Aurore::Util::ResourceLocation::Parse(*channel_text);
				if (!channel)
					return std::unexpected(ProtocolError::MalformedPacket);

				const auto payload_size = reader.Remaining();
				if (payload_size > limits.MaximumCustomPayloadBytes)
					return std::unexpected(ProtocolError::MalformedPacket);

				auto payload = reader.ReadBytes(payload_size);
				if (!payload)
					return std::unexpected(ProtocolError::MalformedPacket);
				if (!reader.Empty())
					return std::unexpected(ProtocolError::TrailingPacketData);

				/*
					minecraft:brand is itself encoded as a Minecraft String inside the
					custom-payload data section. Decode it here so malformed brand adata
					cannot enter session state.
				*/
				if (IsClientBrandChannel(*channel)) {
					Aurore::Util::ByteReader brand_reader(*payload);
					auto brand = brand_reader.ReadString(limits.MaximumClientBrandEncodedBytes);
					if (!brand)
						return std::unexpected(ProtocolError::MalformedPacket);
					if (!brand_reader.Empty())
						return std::unexpected(ProtocolError::TrailingPacketData);
					return Packet{ ClientBrand{.Brand = std::move(*brand)} };
				}
				return Packet{
					CustomPayload{
						.Channel = std::move(*channel),
						.Data = std::vector<std::byte>(payload->begin(), payload->end())
					}
				};
			}

			case SelectKnownPacksPacketId: {
				Aurore::Util::ByteReader reader(frame.Payload);

				auto packs = ReadKnownPacks(reader, limits);
				if (!packs)
					return std::unexpected(packs.error());

				if (!reader.Empty())
					return std::unexpected(
						ProtocolError::TrailingPacketData);

				return Packet{
					SelectKnownPacks{
						.Packs = std::move(*packs),
					}
				};
			}

			case FinishConfigurationPacketId:
				if (!frame.Payload.empty())
					return std::unexpected(
						ProtocolError::TrailingPacketData);

				return Packet{ FinishConfiguration{} };

			default:
				return std::unexpected(
					ProtocolError::UnexpectedPacket);
			}
		}
	}
}
