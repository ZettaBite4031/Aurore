#include <Aurore/Protocol/Packets/Configuration.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/NbtBinary.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		namespace Configuration =
			Aurore::Protocol::Packets::Configuration;

		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Util::ByteBuffer;
		using Aurore::Util::ByteReader;

		PacketFrame MakeFrame(
			std::int32_t packet_id,
			const ByteBuffer& payload) {

			return PacketFrame{
				.PacketId = packet_id,
				.Payload = std::vector<std::byte>(
					payload.Bytes().begin(),
					payload.Bytes().end()),
			};
		}

		std::vector<std::byte> MakeBytes(
			std::initializer_list<std::uint8_t> values) {

			std::vector<std::byte> result;
			result.reserve(values.size());

			for (const auto value : values)
				result.push_back(static_cast<std::byte>(value));

			return result;
		}

		void ExpectBytesEqual(
			std::span<const std::byte> actual,
			std::span<const std::byte> expected) {

			ASSERT_EQ(actual.size(), expected.size());
			EXPECT_TRUE(std::equal(
				actual.begin(),
				actual.end(),
				expected.begin()));
		}

		Aurore::Util::ResourceLocation Location(
			std::string_view value) {

			auto result =
				Aurore::Util::ResourceLocation::Parse(value);

			if (!result)
				throw std::logic_error(
					"Invalid test resource location");

			return std::move(*result);
		}

		Aurore::Util::NbtCompound MakeRegistryEntryData(
			std::int32_t value = 42) {

			Aurore::Util::NbtCompound data;
			data.Set(
				Aurore::Util::NbtString{ u"x" },
				Aurore::Util::NbtValue::Int(value));

			return data;
		}
	}

	TEST(ConfigurationProtocolContractTests, TargetsProtocol774) {
		EXPECT_EQ(Configuration::TargetProtocolVersion, 774);
	}

	TEST(ConfigurationProtocolContractTests, LocksClientboundPacketIds) {
		ASSERT_EQ(
			Configuration::Clientbound::PacketDescriptors.size(),
			20u);

		for (std::size_t index{ 0 };
			index
			< Configuration::Clientbound::
			PacketDescriptors.size();
			++index) {

			const auto& descriptor =
				Configuration::Clientbound::
				PacketDescriptors[index];

			EXPECT_EQ(
				descriptor.Id,
				static_cast<std::int32_t>(index));

			EXPECT_EQ(
				descriptor.PacketDirection,
				Configuration::Direction::Clientbound);
		}

		EXPECT_EQ(
			Configuration::Clientbound::
			RegistryDataPacketId,
			0x07);

		EXPECT_EQ(
			Configuration::Clientbound::
			FeatureFlagsPacketId,
			0x0C);

		EXPECT_EQ(
			Configuration::Clientbound::TagsPacketId,
			0x0D);

		EXPECT_EQ(
			Configuration::Clientbound::
			SelectKnownPacksPacketId,
			0x0E);
	}

	TEST(ConfigurationProtocolContractTests, LocksServerboundPacketIds) {
		ASSERT_EQ(
			Configuration::Serverbound::PacketDescriptors.size(),
			10u);

		for (std::size_t index{ 0 };
			index
			< Configuration::Serverbound::
			PacketDescriptors.size();
			++index) {

			const auto& descriptor =
				Configuration::Serverbound::
				PacketDescriptors[index];

			EXPECT_EQ(
				descriptor.Id,
				static_cast<std::int32_t>(index));

			EXPECT_EQ(
				descriptor.PacketDirection,
				Configuration::Direction::Serverbound);
		}

		EXPECT_EQ(
			Configuration::Serverbound::
			ClientInformationPacketId,
			0x00);

		EXPECT_EQ(
			Configuration::Serverbound::
			FinishConfigurationPacketId,
			0x03);

		EXPECT_EQ(
			Configuration::Serverbound::
			SelectKnownPacksPacketId,
			0x07);
	}

	TEST(ConfigurationProtocolContractTests, LocksSyntheticBootstrapOrder) {
		constexpr std::array expected{
			Configuration::SequenceStep::SendFeatureFlags,
			Configuration::SequenceStep::SendSelectKnownPacks,
			Configuration::SequenceStep::ReceiveSelectKnownPacks,
			Configuration::SequenceStep::SendRegistryData,
			Configuration::SequenceStep::SendTags,
			Configuration::SequenceStep::SendFinishConfiguration,
			Configuration::SequenceStep::ReceiveFinishConfiguration,
		};

		EXPECT_EQ(
			Configuration::SyntheticConfigurationOrder,
			expected);
	}

	TEST(ConfigurationClientInformationTests, DecodesProtocol774Fields) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(12);
		payload.WriteVarInt(
			static_cast<std::int32_t>(
				Configuration::ChatMode::CommandsOnly));
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0x7F);
		payload.WriteVarInt(
			static_cast<std::int32_t>(
				Configuration::MainHand::Right));
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(
			static_cast<std::int32_t>(
				Configuration::ParticleStatus::Decreased));

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_TRUE(result.has_value());
		ASSERT_TRUE(std::holds_alternative<
			Configuration::Serverbound::
			ClientInformation>(*result));

		const auto& packet = std::get<
			Configuration::Serverbound::
			ClientInformation>(*result);

		EXPECT_EQ(packet.Locale, "en_us");
		EXPECT_EQ(packet.ViewDistance, 12);
		EXPECT_EQ(
			packet.ChatModeValue,
			Configuration::ChatMode::CommandsOnly);
		EXPECT_TRUE(packet.ChatColors);
		EXPECT_EQ(packet.SkinParts, 0x7F);
		EXPECT_EQ(
			packet.MainHandValue,
			Configuration::MainHand::Right);
		EXPECT_FALSE(packet.EnableTextFiltering);
		EXPECT_TRUE(packet.EnableServerListing);
		EXPECT_EQ(
			packet.ParticleStatusValue,
			Configuration::ParticleStatus::Decreased);
	}

	TEST(ConfigurationClientInformationTests, PreservesSignedViewDistance) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(0xFF);
		payload.WriteVarInt(0);
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0);
		payload.WriteVarInt(0);
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(0);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_TRUE(result.has_value());

		const auto& packet = std::get<
			Configuration::Serverbound::
			ClientInformation>(*result);

		EXPECT_EQ(packet.ViewDistance, -1);
	}

	TEST(ConfigurationClientInformationTests, RejectsInvalidChatMode) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(10);
		payload.WriteVarInt(3);
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0);
		payload.WriteVarInt(0);
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(0);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(ConfigurationClientInformationTests, RejectsInvalidMainHand) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(10);
		payload.WriteVarInt(0);
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0);
		payload.WriteVarInt(2);
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(0);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(ConfigurationClientInformationTests, RejectsInvalidParticleStatus) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(10);
		payload.WriteVarInt(0);
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0);
		payload.WriteVarInt(0);
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(3);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(ConfigurationClientInformationTests, RejectsTrailingData) {
		ByteBuffer payload;
		payload.WriteString("en_us");
		payload.WriteUnsignedByte(10);
		payload.WriteVarInt(0);
		payload.WriteBool(true);
		payload.WriteUnsignedByte(0);
		payload.WriteVarInt(0);
		payload.WriteBool(false);
		payload.WriteBool(true);
		payload.WriteVarInt(0);
		payload.WriteUnsignedByte(0xAA);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					ClientInformationPacketId,
					payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::TrailingPacketData);
	}

	TEST(ConfigurationKnownPacksCodecTests, EncodesEmptyOfferExactly) {
		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::
				SelectKnownPacks{});

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(
			result->PacketId,
			Configuration::Clientbound::
			SelectKnownPacksPacketId);

		const std::array expected{
			static_cast<std::byte>(0x00)
		};

		ExpectBytesEqual(
			result->Payload,
			expected);
	}

	TEST(ConfigurationKnownPacksCodecTests, EncodesKnownPackFieldsInOrder) {
		const Configuration::KnownPack pack{
			.Namespace = "aurore_test",
			.Id = "synthetic",
			.Version = "1",
		};

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::
				SelectKnownPacks{
					.Packs = { pack },
				});

		ASSERT_TRUE(result.has_value());

		ByteReader reader(result->Payload);

		const auto count = reader.ReadVarInt();
		const auto namespace_name =
			reader.ReadString(64);
		const auto id = reader.ReadString(256);
		const auto version = reader.ReadString(64);

		ASSERT_TRUE(count.has_value());
		ASSERT_TRUE(namespace_name.has_value());
		ASSERT_TRUE(id.has_value());
		ASSERT_TRUE(version.has_value());

		EXPECT_EQ(*count, 1);
		EXPECT_EQ(*namespace_name, pack.Namespace);
		EXPECT_EQ(*id, pack.Id);
		EXPECT_EQ(*version, pack.Version);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ConfigurationKnownPacksCodecTests, RejectsOversizedOffer) {
		Configuration::Limits limits;
		limits.MaximumKnownPacks = 1;

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::
				SelectKnownPacks{
					.Packs = {
						{ "aurore_test", "first", "1" },
						{ "aurore_test", "second", "1" },
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			KnownPackLimitExceeded);
		EXPECT_FALSE(
			result.error().PackIndex.has_value());
	}

	TEST(ConfigurationKnownPacksCodecTests, DecodesServerSelection) {
		ByteBuffer payload;
		payload.WriteVarInt(1);
		payload.WriteString("aurore_test");
		payload.WriteString("synthetic");
		payload.WriteString("1");

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					SelectKnownPacksPacketId,
					payload));

		ASSERT_TRUE(result.has_value());

		const auto& packet = std::get<
			Configuration::Serverbound::
			SelectKnownPacks>(*result);

		ASSERT_EQ(packet.Packs.size(), 1u);
		EXPECT_EQ(
			packet.Packs[0],
			(Configuration::KnownPack{
				.Namespace = "aurore_test",
				.Id = "synthetic",
				.Version = "1",
				}));
	}

	TEST(ConfigurationKnownPacksCodecTests, RejectsNegativePackCount) {
		ByteBuffer payload;
		payload.WriteVarInt(-1);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					SelectKnownPacksPacketId,
					payload));

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(ConfigurationKnownPacksContractTests, AcceptsSelectedSubset) {
		const std::vector offered{
			Configuration::KnownPack{
				.Namespace = "aurore_test",
				.Id = "first",
				.Version = "1",
			},
			Configuration::KnownPack{
				.Namespace = "aurore_test",
				.Id = "second",
				.Version = "1",
			},
		};

		const std::vector selected{
			offered[1]
		};

		EXPECT_TRUE(
			Configuration::ValidateKnownPackSelection(
				offered,
				selected).has_value());
	}

	TEST(ConfigurationKnownPacksContractTests, RejectsDuplicateSelection) {
		const Configuration::KnownPack pack{
			.Namespace = "aurore_test",
			.Id = "synthetic",
			.Version = "1",
		};

		const std::vector offered{ pack };
		const std::vector selected{ pack, pack };

		const auto result =
			Configuration::ValidateKnownPackSelection(
				offered,
				selected);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::ContractErrorCode::
			DuplicateSelectedPack);
		EXPECT_EQ(result.error().SelectedIndex, 1u);
		EXPECT_EQ(result.error().ExistingIndex, 0u);
	}

	TEST(ConfigurationKnownPacksContractTests, RejectsUnofferedPack) {
		const std::vector<Configuration::KnownPack> offered;
		const std::vector selected{
			Configuration::KnownPack{
				.Namespace = "minecraft",
				.Id = "core",
				.Version = "1.21.11",
			}
		};

		const auto result =
			Configuration::ValidateKnownPackSelection(
				offered,
				selected);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::ContractErrorCode::
			UnofferedPack);
		EXPECT_EQ(result.error().SelectedIndex, 0u);
		EXPECT_FALSE(
			result.error().ExistingIndex.has_value());
	}

	TEST(ConfigurationFinishCodecTests, EncodesAndDecodesEmptyPackets) {
		const auto clientbound =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::
				FinishConfiguration{});

		EXPECT_EQ(
			clientbound.PacketId,
			Configuration::Clientbound::
			FinishConfigurationPacketId);

		EXPECT_TRUE(clientbound.Payload.empty());

		const PacketFrame serverbound{
			.PacketId =
				Configuration::Serverbound::
					FinishConfigurationPacketId,
			.Payload = {},
		};

		const auto decoded =
			Configuration::Serverbound::Decode(
				serverbound);

		ASSERT_TRUE(decoded.has_value());
		EXPECT_TRUE(std::holds_alternative<
			Configuration::Serverbound::
			FinishConfiguration>(*decoded));
	}

	TEST(ConfigurationFinishCodecTests, RejectsTrailingFinishData) {
		const PacketFrame frame{
			.PacketId =
				Configuration::Serverbound::
					FinishConfigurationPacketId,
			.Payload = {
				static_cast<std::byte>(0x00)
			},
		};

		const auto result =
			Configuration::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::TrailingPacketData);
	}

	TEST(ConfigurationCodecTests, RejectsUnsupportedPacketId) {
		const PacketFrame frame{
			.PacketId = 0x7F,
			.Payload = {},
		};

		const auto result =
			Configuration::Serverbound::Decode(frame);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error(),
			ProtocolError::UnexpectedPacket);
	}


	TEST(ConfigurationFeatureFlagsCodecTests, EncodesEmptyFlagsExactly) {
		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::FeatureFlags{});

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(
			result->PacketId,
			Configuration::Clientbound::FeatureFlagsPacketId);

		const std::array expected{
			static_cast<std::byte>(0x00)
		};

		ExpectBytesEqual(result->Payload, expected);
	}

	TEST(ConfigurationFeatureFlagsCodecTests, EncodesFlagsInDeclarationOrder) {
		const auto vanilla = Location("minecraft:vanilla");
		const auto synthetic = Location("aurore_test:feature");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::FeatureFlags{
					.Features = {
						vanilla,
						synthetic,
					},
				});

		ASSERT_TRUE(result.has_value());

		ByteBuffer expected;
		expected.WriteVarInt(2);
		expected.WriteString(vanilla.ToString());
		expected.WriteString(synthetic.ToString());

		ExpectBytesEqual(
			result->Payload,
			expected.Bytes());
	}

	TEST(ConfigurationFeatureFlagsCodecTests, RejectsFeatureCountLimit) {
		Configuration::Limits limits;
		limits.MaximumFeatureFlags = 1;

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::FeatureFlags{
					.Features = {
						Location("aurore_test:first"),
						Location("aurore_test:second"),
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			FeatureFlagLimitExceeded);
		EXPECT_EQ(result.error().ObservedValue, 2u);
		EXPECT_EQ(result.error().LimitValue, 1u);
	}

	TEST(ConfigurationFeatureFlagsCodecTests, ReportsOversizedIdentifier) {
		Configuration::Limits limits;
		limits.MaximumIdentifierEncodedBytes = 8;

		const auto identifier = Location("aurore_test:feature");
		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::FeatureFlags{
					.Features = { identifier },
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			IdentifierLimitExceeded);
		EXPECT_EQ(result.error().FeatureIndex, 0u);
		EXPECT_EQ(result.error().Identifier, identifier);
	}

	TEST(ConfigurationRegistryDataCodecTests, EncodesEmptyRegistryExactly) {
		const auto registry_key =
			Location("aurore_test:empty_registry");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey = registry_key,
					.Entries = {},
				});

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(
			result->PacketId,
			Configuration::Clientbound::RegistryDataPacketId);

		ByteBuffer expected;
		expected.WriteString(registry_key.ToString());
		expected.WriteVarInt(0);

		ExpectBytesEqual(
			result->Payload,
			expected.Bytes());
	}

	TEST(ConfigurationRegistryDataCodecTests, EncodesAbsentAndPresentValuesExactly) {
		const auto registry_key =
			Location("aurore_test:test_registry");
		const auto absent_key =
			Location("aurore_test:without_data");
		const auto present_key =
			Location("aurore_test:with_data");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey = registry_key,
					.Entries = {
						{
							.Key = absent_key,
							.Value = std::nullopt,
						},
						{
							.Key = present_key,
							.Value = MakeRegistryEntryData(),
						},
					},
				});

		ASSERT_TRUE(result.has_value());

		ByteBuffer expected;
		expected.WriteString(registry_key.ToString());
		expected.WriteVarInt(2);
		expected.WriteString(absent_key.ToString());
		expected.WriteBool(false);
		expected.WriteString(present_key.ToString());
		expected.WriteBool(true);
		expected.WriteBytes(MakeBytes({
			0x0A,
			0x03,
			0x00, 0x01, 0x78,
			0x00, 0x00, 0x00, 0x2A,
			0x00,
			}));

		ExpectBytesEqual(
			result->Payload,
			expected.Bytes());
	}

	TEST(ConfigurationRegistryDataCodecTests, PreservesEntryOrder) {
		const auto first = Location("aurore_test:first");
		const auto second = Location("aurore_test:second");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey =
						Location("aurore_test:registry"),
					.Entries = {
						{.Key = first, .Value = std::nullopt },
						{.Key = second, .Value = std::nullopt },
					},
				});

		ASSERT_TRUE(result.has_value());

		ByteReader reader(result->Payload);
		ASSERT_TRUE(reader.ReadString(128).has_value());

		const auto count = reader.ReadVarInt();
		ASSERT_TRUE(count.has_value());
		EXPECT_EQ(*count, 2);

		const auto first_value = reader.ReadString(128);
		const auto first_present = reader.ReadBool();
		const auto second_value = reader.ReadString(128);
		const auto second_present = reader.ReadBool();

		ASSERT_TRUE(first_value.has_value());
		ASSERT_TRUE(first_present.has_value());
		ASSERT_TRUE(second_value.has_value());
		ASSERT_TRUE(second_present.has_value());

		EXPECT_EQ(*first_value, first.ToString());
		EXPECT_FALSE(*first_present);
		EXPECT_EQ(*second_value, second.ToString());
		EXPECT_FALSE(*second_present);
		EXPECT_TRUE(reader.Empty());
	}

	TEST(ConfigurationRegistryDataCodecTests, RejectsEntryCountLimit) {
		Configuration::Limits limits;
		limits.MaximumRegistryEntries = 1;

		const auto registry_key =
			Location("aurore_test:registry");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey = registry_key,
					.Entries = {
						{
							.Key = Location("aurore_test:first"),
							.Value = std::nullopt,
						},
						{
							.Key = Location("aurore_test:second"),
							.Value = std::nullopt,
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			RegistryEntryLimitExceeded);
		EXPECT_EQ(result.error().RegistryKey, registry_key);
	}

	TEST(ConfigurationRegistryDataCodecTests, ReportsNbtWriteFailureContext) {
		Configuration::Limits limits;
		limits.RegistryEntryNbt.MaximumTotalBytes = 1;

		const auto registry_key =
			Location("aurore_test:registry");
		const auto entry_key =
			Location("aurore_test:entry");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey = registry_key,
					.Entries = {
						{
							.Key = entry_key,
							.Value = MakeRegistryEntryData(),
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			RegistryEntryNbtEncodingFailed);
		EXPECT_EQ(result.error().EntryIndex, 0u);
		EXPECT_EQ(result.error().RegistryKey, registry_key);
		EXPECT_EQ(result.error().EntryKey, entry_key);
		ASSERT_TRUE(result.error().NbtError.has_value());
		EXPECT_EQ(
			result.error().NbtError->Code,
			Aurore::Util::NbtWriteErrorCode::
			TotalByteLimitExceeded);
	}

	TEST(ConfigurationRegistryDataCodecTests, RejectsCompletePacketSizeLimit) {
		Configuration::Limits limits;
		limits.MaximumPacketDataBytes = 1;

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::RegistryData{
					.RegistryKey =
						Location("aurore_test:registry"),
					.Entries = {},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			PacketSizeExceeded);
		EXPECT_GT(result.error().ObservedValue, 1u);
		EXPECT_EQ(result.error().LimitValue, 1u);
	}

	TEST(ConfigurationUpdateTagsCodecTests, EncodesEmptyPacketExactly) {
		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{});

		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(
			result->PacketId,
			Configuration::Clientbound::TagsPacketId);

		const std::array expected{
			static_cast<std::byte>(0x00)
		};

		ExpectBytesEqual(result->Payload, expected);
	}

	TEST(ConfigurationUpdateTagsCodecTests, EncodesRegistryTagAndMemberOrderExactly) {
		const auto first_registry =
			Location("aurore_test:first_registry");
		const auto second_registry =
			Location("aurore_test:second_registry");
		const auto first_tag =
			Location("aurore_test:first_tag");
		const auto second_tag =
			Location("aurore_test:second_tag");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey = first_registry,
							.Tags = {
								{
									.Key = first_tag,
									.Members = { 2, 0, 1 },
								},
								{
									.Key = second_tag,
									.Members = {},
								},
							},
						},
						{
							.RegistryKey = second_registry,
							.Tags = {},
						},
					},
				});

		ASSERT_TRUE(result.has_value());

		ByteBuffer expected;
		expected.WriteVarInt(2);
		expected.WriteString(first_registry.ToString());
		expected.WriteVarInt(2);
		expected.WriteString(first_tag.ToString());
		expected.WriteVarInt(3);
		expected.WriteVarInt(2);
		expected.WriteVarInt(0);
		expected.WriteVarInt(1);
		expected.WriteString(second_tag.ToString());
		expected.WriteVarInt(0);
		expected.WriteString(second_registry.ToString());
		expected.WriteVarInt(0);

		ExpectBytesEqual(
			result->Payload,
			expected.Bytes());
	}

	TEST(ConfigurationUpdateTagsCodecTests, RejectsRegistryCountLimit) {
		Configuration::Limits limits;
		limits.MaximumTagRegistries = 1;

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey =
								Location("aurore_test:first"),
							.Tags = {},
						},
						{
							.RegistryKey =
								Location("aurore_test:second"),
							.Tags = {},
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			TagRegistryLimitExceeded);
	}

	TEST(ConfigurationUpdateTagsCodecTests, RejectsPerRegistryTagLimit) {
		Configuration::Limits limits;
		limits.MaximumTagsPerRegistry = 1;

		const auto registry_key =
			Location("aurore_test:registry");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey = registry_key,
							.Tags = {
								{
									.Key = Location("aurore_test:first"),
									.Members = {},
								},
								{
									.Key = Location("aurore_test:second"),
									.Members = {},
								},
							},
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			TagLimitExceeded);
		EXPECT_EQ(result.error().RegistryIndex, 0u);
		EXPECT_EQ(result.error().RegistryKey, registry_key);
	}

	TEST(ConfigurationUpdateTagsCodecTests, RejectsPerTagMemberLimit) {
		Configuration::Limits limits;
		limits.MaximumMembersPerTag = 1;

		const auto tag_key = Location("aurore_test:tag");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey =
								Location("aurore_test:registry"),
							.Tags = {
								{
									.Key = tag_key,
									.Members = { 0, 1 },
								},
							},
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			TagMemberLimitExceeded);
		EXPECT_EQ(result.error().TagIndex, 0u);
		EXPECT_EQ(result.error().TagKey, tag_key);
	}

	TEST(ConfigurationUpdateTagsCodecTests, RejectsTotalMemberLimit) {
		Configuration::Limits limits;
		limits.MaximumTotalTagMembers = 2;

		const auto second_tag =
			Location("aurore_test:second");

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey =
								Location("aurore_test:registry"),
							.Tags = {
								{
									.Key = Location("aurore_test:first"),
									.Members = { 0, 1 },
								},
								{
									.Key = second_tag,
									.Members = { 2 },
								},
							},
						},
					},
				},
				limits);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			TotalTagMemberLimitExceeded);
		EXPECT_EQ(result.error().TagIndex, 1u);
		EXPECT_EQ(result.error().TagKey, second_tag);
		EXPECT_EQ(result.error().ObservedValue, 3u);
	}

	TEST(ConfigurationUpdateTagsCodecTests, RejectsRuntimeIdOverflow) {
		const auto tag_key = Location("aurore_test:tag");
		const auto overflowing_id =
			static_cast<Aurore::Util::RegistryRuntimeId>(
				std::numeric_limits<std::int32_t>::max()) + 1u;

		const auto result =
			Configuration::Clientbound::Encode(
				Configuration::Clientbound::UpdateTags{
					.Registries = {
						{
							.RegistryKey =
								Location("aurore_test:registry"),
							.Tags = {
								{
									.Key = tag_key,
									.Members = {
										overflowing_id,
									},
								},
							},
						},
					},
				});

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(
			result.error().Code,
			Configuration::EncodeErrorCode::
			RuntimeIdEncodingOverflow);
		EXPECT_EQ(result.error().MemberIndex, 0u);
		EXPECT_EQ(result.error().TagKey, tag_key);
	}

	TEST(ConfigurationClientboundVariantTests, DispatchesM52Packets) {
		const std::array<Configuration::Clientbound::Packet, 3> packets{
			Configuration::Clientbound::FeatureFlags{},
			Configuration::Clientbound::RegistryData{
				.RegistryKey = Location("aurore_test:registry"),
				.Entries = {},
			},
			Configuration::Clientbound::UpdateTags{},
		};

		const std::array expected_ids{
			Configuration::Clientbound::FeatureFlagsPacketId,
			Configuration::Clientbound::RegistryDataPacketId,
			Configuration::Clientbound::TagsPacketId,
		};

		for (std::size_t index{ 0 }; index < packets.size(); ++index) {
			const auto result =
				Configuration::Clientbound::Encode(packets[index]);

			ASSERT_TRUE(result.has_value());
			EXPECT_EQ(result->PacketId, expected_ids[index]);
		}
	}

	TEST(
		ConfigurationCustomPayloadTests,
		DecodesClientBrandPayload) {

		ByteBuffer payload;
		payload.WriteString("minecraft:brand");

		ByteBuffer brand_payload;
		brand_payload.WriteString("vanilla");

		payload.WriteBytes(
			brand_payload.Bytes());

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					CustomPayloadPacketId,
					payload));

		ASSERT_TRUE(result.has_value());

		ASSERT_TRUE(
			std::holds_alternative<
			Configuration::Serverbound::
			ClientBrand>(*result));

		const auto& brand =
			std::get<
			Configuration::Serverbound::
			ClientBrand>(*result);

		EXPECT_EQ(brand.Brand, "vanilla");
	}

	TEST(
		ConfigurationCustomPayloadTests,
		PreservesUnknownBoundedPayload) {

		ByteBuffer payload;
		payload.WriteString("example:test");

		const auto expected_data =
			MakeBytes({ 0x01, 0x02, 0x03 });

		payload.WriteBytes(expected_data);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					CustomPayloadPacketId,
					payload));

		ASSERT_TRUE(result.has_value());

		ASSERT_TRUE(
			std::holds_alternative<
			Configuration::Serverbound::
			CustomPayload>(*result));

		const auto& custom =
			std::get<
			Configuration::Serverbound::
			CustomPayload>(*result);

		EXPECT_EQ(
			custom.Channel.ToString(),
			"example:test");

		ExpectBytesEqual(
			custom.Data,
			expected_data);
	}

	TEST(
		ConfigurationCustomPayloadTests,
		RejectsMalformedClientBrandData) {

		ByteBuffer payload;
		payload.WriteString("minecraft:brand");

		/*
			Declares a five-byte string but provides no string data.
		*/
		payload.WriteVarInt(5);

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					CustomPayloadPacketId,
					payload));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

	TEST(
		ConfigurationCustomPayloadTests,
		RejectsInvalidChannelIdentifier) {

		ByteBuffer payload;
		payload.WriteString("Invalid Namespace:test");

		const auto result =
			Configuration::Serverbound::Decode(
				MakeFrame(
					Configuration::Serverbound::
					CustomPayloadPacketId,
					payload));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error(),
			ProtocolError::MalformedPacket);
	}

}
