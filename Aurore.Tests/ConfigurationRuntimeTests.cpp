#include <Aurore/Core/ClientLifecycle.hpp>
#include <Aurore/Core/RegistrySnapshotStore.hpp>
#include <Aurore/Core/SyntheticRegistrySnapshot.hpp>

#include <Aurore/Protocol/ConfigurationSequence.hpp>
#include <Aurore/Protocol/PacketFrame.hpp>
#include <Aurore/Protocol/Packets/Configuration.hpp>
#include <Aurore/Protocol/Packets/Login.hpp>
#include <Aurore/Protocol/ProtocolConnection.hpp>
#include <Aurore/Protocol/ProtocolSession.hpp>

#include <Aurore/Util/ByteBuffer.hpp>
#include <Aurore/Util/ResourceLocation.hpp>
#include <Aurore/Util/UUID.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Aurore::Tests {
	namespace {
		namespace Configuration =
			Aurore::Protocol::Packets::Configuration;

		namespace Login =
			Aurore::Protocol::Packets::Login;

		using Aurore::Core::ClientConnection;
		using Aurore::Core::ClientLifecycleState;
		using Aurore::Core::RegistrySnapshotStore;
		using Aurore::Core::SyntheticRegistrySnapshotFactory;

		using Aurore::Network::ConnectionId;
		using Aurore::Network::NetworkEndpoint;

		using Aurore::Protocol::ConfigurationAcceptedResolution;
		using Aurore::Protocol::ConfigurationCompleteRequest;
		using Aurore::Protocol::ConfigurationReadyResolution;
		using Aurore::Protocol::ConfigurationRejectedResolution;
		using Aurore::Protocol::ConfigurationResolution;
		using Aurore::Protocol::ConfigurationSequenceBuilder;
		using Aurore::Protocol::ConfigurationSequencePolicy;
		using Aurore::Protocol::ConfigurationStartRequest;
		using Aurore::Protocol::ConnectionDisposition;
		using Aurore::Protocol::EncodePacketFrame;
		using Aurore::Protocol::HandshakeIntention;
		using Aurore::Protocol::LoginAcceptedResolution;
		using Aurore::Protocol::LoginStartRequest;
		using Aurore::Protocol::PacketFrame;
		using Aurore::Protocol::PacketFrameDecoder;
		using Aurore::Protocol::ProtocolConnection;
		using Aurore::Protocol::ProtocolError;
		using Aurore::Protocol::ProtocolProcessingStage;
		using Aurore::Protocol::ProtocolRequest;
		using Aurore::Protocol::ProtocolRequestId;
		using Aurore::Protocol::ProtocolState;

		using Aurore::Util::ByteBuffer;
		using Aurore::Util::RegistryGeneration;
		using Aurore::Util::RegistrySnapshot;
		using Aurore::Util::ResourceLocation;
		using Aurore::Util::Uuid;

		[[nodiscard]] ResourceLocation ParseRequired(
			std::string_view value) {

			auto result = ResourceLocation::Parse(value);

			if (!result)
				throw std::runtime_error(
					"Invalid ResourceLocation test fixture.");

			return std::move(*result);
		}

		[[nodiscard]] std::shared_ptr<
			const RegistrySnapshot>
		BuildSyntheticSnapshot(
			RegistryGeneration generation) {

			auto result =
				SyntheticRegistrySnapshotFactory::Build(
					generation);

			if (!result)
				throw std::runtime_error(
					"Synthetic snapshot test fixture failed.");

			return std::move(*result);
		}

		[[nodiscard]] ConfigurationSequencePolicy
		MakePolicy() {
			return ConfigurationSequencePolicy{
				.EnabledFeatures = {
					ParseRequired("minecraft:vanilla"),
				},
				.KnownPacks = {},
			};
		}

		[[nodiscard]] ByteBuffer
		MakeLoginHandshake() {
			ByteBuffer payload;

			payload.WriteVarInt(774);
			payload.WriteString("localhost");
			payload.WriteUInt16(25565);
			payload.WriteVarInt(
				static_cast<std::int32_t>(
					HandshakeIntention::Login));

			return EncodePacketFrame(
				0x00,
				payload.Bytes());
		}

		[[nodiscard]] ByteBuffer
		MakeLoginStart(
			std::string_view username) {

			ByteBuffer payload;
			payload.WriteString(username);
			payload.WriteUuid({});

			return EncodePacketFrame(
				Login::Serverbound::StartPacketId,
				payload.Bytes());
		}

		[[nodiscard]] ByteBuffer
		MakeLoginAcknowledged() {
			return EncodePacketFrame(
				Login::Serverbound::
					AcknowledgedPacketId,
				std::span<const std::byte>{});
		}

		[[nodiscard]] ByteBuffer
		MakeClientInformation() {
			ByteBuffer payload;

			payload.WriteString("en_us");
			payload.WriteUnsignedByte(12);
			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::ChatMode::Enabled));
			payload.WriteBool(true);
			payload.WriteUnsignedByte(0x7F);
			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::MainHand::Right));
			payload.WriteBool(false);
			payload.WriteBool(true);
			payload.WriteVarInt(
				static_cast<std::int32_t>(
					Configuration::ParticleStatus::All));

			return EncodePacketFrame(
				Configuration::Serverbound::
					ClientInformationPacketId,
				payload.Bytes());
		}

		[[nodiscard]] ByteBuffer
		MakeKnownPackSelection(
			std::span<const Configuration::KnownPack> packs = {}) {

			ByteBuffer payload;

			payload.WriteVarInt(
				static_cast<std::int32_t>(
					packs.size()));

			for (const auto& pack : packs) {
				payload.WriteString(pack.Namespace);
				payload.WriteString(pack.Id);
				payload.WriteString(pack.Version);
			}

			return EncodePacketFrame(
				Configuration::Serverbound::
					SelectKnownPacksPacketId,
				payload.Bytes());
		}

		[[nodiscard]] ByteBuffer
		MakeFinishConfiguration() {
			return EncodePacketFrame(
				Configuration::Serverbound::
					FinishConfigurationPacketId,
				std::span<const std::byte>{});
		}

		[[nodiscard]] std::vector<std::byte>
		Combine(
			std::span<const std::byte> first,
			std::span<const std::byte> second) {

			std::vector<std::byte> result;
			result.reserve(
				first.size() + second.size());

			result.insert(
				result.end(),
				first.begin(),
				first.end());

			result.insert(
				result.end(),
				second.begin(),
				second.end());

			return result;
		}

		[[nodiscard]] std::vector<PacketFrame>
		DecodeFrames(const ByteBuffer& batch) {
			PacketFrameDecoder decoder;
			decoder.Append(batch.Bytes());

			std::vector<PacketFrame> result;

			while (true) {
				auto frame = decoder.TryDecode();

				if (!frame)
					throw std::runtime_error(
						"Failed to decode test output batch.");

				if (!frame->has_value())
					break;

				result.push_back(
					std::move(frame->value()));
			}

			if (!decoder.Empty())
				throw std::runtime_error(
					"Test output batch retained partial data.");

			return result;
		}

		template<typename TRequest>
		[[nodiscard]] TRequest GetRequest(
			const ProtocolRequest& request) {

			if (!std::holds_alternative<TRequest>(request))
				throw std::runtime_error(
					"Unexpected protocol request type.");

			return std::get<TRequest>(request);
		}

		struct ConfigurationFixture final {
			ProtocolConnection Connection;
			ConfigurationStartRequest StartRequest;
		};

		[[nodiscard]] ConfigurationFixture
		EnterConfiguration(
			bool coalesce_client_information = false) {

			ProtocolConnection connection;

			const auto login_input = Combine(
				MakeLoginHandshake().Bytes(),
				MakeLoginStart("PlayerOne").Bytes());

			auto login_start =
				connection.Receive(login_input);

			if (login_start.HasError()
				|| login_start.Requests.size() != 1) {

				throw std::runtime_error(
					"Login start test fixture failed.");
			}

			const auto login_request =
				GetRequest<LoginStartRequest>(
					login_start.Requests.front());

			auto login_resolution =
				connection.ResolveLogin(
					LoginAcceptedResolution{
						.Id = login_request.Id,
						.ProfileId =
							Uuid::FromOfflinePlayerName(
								"PlayerOne"),
						.Username = "PlayerOne",
						.Properties = {},
					});

			if (login_resolution.HasError())
				throw std::runtime_error(
					"Login resolution test fixture failed.");

			const auto login_acknowledged =
				MakeLoginAcknowledged();

			const auto acknowledgement =
				coalesce_client_information
				? Combine(
					login_acknowledged.Bytes(),
					MakeClientInformation().Bytes())
				: std::vector<std::byte>(
					login_acknowledged.Bytes().begin(),
					login_acknowledged.Bytes().end());

			auto configuration_start =
				connection.Receive(acknowledgement);

			if (configuration_start.HasError()
				|| configuration_start.Requests.size() != 1) {

				throw std::runtime_error(
					"Configuration start test fixture failed.");
			}

			return ConfigurationFixture{
				.Connection = std::move(connection),
				.StartRequest =
					GetRequest<ConfigurationStartRequest>(
						configuration_start.Requests.front()),
			};
		}

		[[nodiscard]] ConfigurationResolution
		BuildReadyResolution(
			ProtocolRequestId id,
			RegistryGeneration generation = 1) {

			auto plan =
				ConfigurationSequenceBuilder::Build(
					BuildSyntheticSnapshot(generation),
					MakePolicy());

			if (!plan)
				throw std::runtime_error(
					"Configuration plan test fixture failed.");

			return ConfigurationResolution{
				ConfigurationReadyResolution{
					.Id = id,
					.Plan = std::move(*plan),
				}
			};
		}

		[[nodiscard]]
		ByteBuffer MakeClientBrand(
			std::string_view brand = "vanilla") {

			ByteBuffer payload;
			payload.WriteString("minecraft:brand");

			ByteBuffer brand_payload;
			brand_payload.WriteString(brand);

			payload.WriteBytes(
				brand_payload.Bytes());

			return EncodePacketFrame(
				Configuration::Serverbound::
				CustomPayloadPacketId,
				payload.Bytes());
		}
	}

	TEST(
		ConfigurationRuntimeTests,
		LoginAcknowledgedRequestsConfigurationBootstrap) {

		auto fixture = EnterConfiguration();

		EXPECT_TRUE(fixture.StartRequest.Id);
		EXPECT_EQ(
			fixture.Connection.GetSession().GetState(),
			ProtocolState::Configuration);

		EXPECT_EQ(
			fixture.Connection.GetSession()
				.GetPendingRequestId(),
			fixture.StartRequest.Id);
	}

	TEST(
		ConfigurationRuntimeTests,
		AcceptsCoalescedClientInformationBeforeBootstrapResolution) {

		auto fixture = EnterConfiguration(true);

		const auto& information =
			fixture.Connection.GetSession()
				.GetClientInformation();

		ASSERT_TRUE(information.has_value());
		EXPECT_EQ(information->Locale, "en_us");
		EXPECT_EQ(information->ViewDistance, 12);
	}

	TEST(
		ConfigurationRuntimeTests,
		BootstrapResolutionPublishesOneInitialBatch) {

		auto fixture = EnterConfiguration();

		auto result =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(result.HasError());
		ASSERT_EQ(result.OutboundFrames.size(), 1u);

		const auto frames =
			DecodeFrames(
				result.OutboundFrames.front());

		ASSERT_EQ(frames.size(), 2u);

		EXPECT_EQ(
			frames[0].PacketId,
			Configuration::Clientbound::
				FeatureFlagsPacketId);

		EXPECT_EQ(
			frames[1].PacketId,
			Configuration::Clientbound::
				SelectKnownPacksPacketId);
	}


	TEST(
		ConfigurationRuntimeTests,
		RejectsConsumedBootstrapPlan) {

		auto fixture = EnterConfiguration();

		auto plan =
			ConfigurationSequenceBuilder::Build(
				BuildSyntheticSnapshot(1),
				MakePolicy());

		ASSERT_TRUE(plan.has_value());

		const auto consumed =
			plan->ReleaseInitialFrames();

		ASSERT_EQ(consumed.size(), 2u);

		auto result =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationReadyResolution{
						.Id = fixture.StartRequest.Id,
						.Plan = std::move(*plan),
					}});

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::MissingConfigurationPlan);
	}

	TEST(
		ConfigurationRuntimeTests,
		RejectsStaleBootstrapResolution) {

		auto fixture = EnterConfiguration();

		auto result =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					ProtocolRequestId{
						fixture.StartRequest.Id.Value + 1
					}));

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::InvalidRequestResolution);

		EXPECT_EQ(
			result.Disposition,
			ConnectionDisposition::CloseImmediately);
	}

	TEST(
		ConfigurationRuntimeTests,
		KnownPackSelectionPublishesOnePostNegotiationBatch) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		auto result =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(result.HasError());
		ASSERT_EQ(result.OutboundFrames.size(), 1u);

		const auto frames =
			DecodeFrames(
				result.OutboundFrames.front());

		ASSERT_EQ(frames.size(), 4u);

		EXPECT_EQ(
			frames[0].PacketId,
			Configuration::Clientbound::
				RegistryDataPacketId);

		EXPECT_EQ(
			frames[1].PacketId,
			Configuration::Clientbound::
				RegistryDataPacketId);

		EXPECT_EQ(
			frames[2].PacketId,
			Configuration::Clientbound::
				TagsPacketId);

		EXPECT_EQ(
			frames[3].PacketId,
			Configuration::Clientbound::
				FinishConfigurationPacketId);
	}

	TEST(
		ConfigurationRuntimeTests,
		RejectsUnofferedKnownPackSelection) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		const std::vector selected{
			Configuration::KnownPack{
				.Namespace = "minecraft",
				.Id = "core",
				.Version = "1.21.11",
			}
		};

		const auto result =
			fixture.Connection.Receive(
				MakeKnownPackSelection(
					selected).Bytes());

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::
				InvalidKnownPackSelection);
	}

	TEST(
		ConfigurationRuntimeTests,
		RejectsFinishBeforeKnownPackSelection) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		const auto result =
			fixture.Connection.Receive(
				MakeFinishConfiguration().Bytes());

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::UnexpectedPacket);
	}

	TEST(
		ConfigurationRuntimeTests,
		FinishAcknowledgementRequestsPlayAdmission) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id,
					7));

		ASSERT_FALSE(bootstrap.HasError());

		auto data =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(data.HasError());

		auto finish =
			fixture.Connection.Receive(
				MakeFinishConfiguration().Bytes());

		ASSERT_FALSE(finish.HasError());
		ASSERT_EQ(finish.Requests.size(), 1u);
		EXPECT_TRUE(finish.OutboundFrames.empty());

		const auto request =
			GetRequest<ConfigurationCompleteRequest>(
				finish.Requests.front());

		EXPECT_TRUE(request.Id);
		EXPECT_EQ(request.Generation, 7u);
	}

	TEST(
		ConfigurationRuntimeTests,
		FinishAcknowledgementEnforcesCoreBoundary) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		auto data =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(data.HasError());

		const auto received = Combine(
			MakeFinishConfiguration().Bytes(),
			EncodePacketFrame(
				0x00,
				std::span<const std::byte>{}).Bytes());

		const auto result =
			fixture.Connection.Receive(received);

		ASSERT_TRUE(result.HasError());
		EXPECT_TRUE(result.Requests.empty());
		ASSERT_TRUE(std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(*result.Error),
			ProtocolError::UnexpectedPacket);
	}

	TEST(
		ConfigurationRuntimeTests,
		AcceptedCompletionTransitionsToPlayAndPinsSnapshot) {

		auto fixture = EnterConfiguration();

		auto snapshot =
			BuildSyntheticSnapshot(9);

		auto plan =
			ConfigurationSequenceBuilder::Build(
				snapshot,
				MakePolicy());

		ASSERT_TRUE(plan.has_value());

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationReadyResolution{
						.Id = fixture.StartRequest.Id,
						.Plan = std::move(*plan),
					}});

		ASSERT_FALSE(bootstrap.HasError());

		auto data =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(data.HasError());

		auto finish =
			fixture.Connection.Receive(
				MakeFinishConfiguration().Bytes());

		ASSERT_FALSE(finish.HasError());

		const auto complete =
			GetRequest<ConfigurationCompleteRequest>(
				finish.Requests.front());

		auto accepted =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationAcceptedResolution{
						.Id = complete.Id,
					}});

		ASSERT_FALSE(accepted.HasError());

		EXPECT_EQ(
			fixture.Connection.GetSession().GetState(),
			ProtocolState::Play);

		ASSERT_TRUE(
			fixture.Connection.GetSession()
				.GetRegistrySnapshot());

		EXPECT_EQ(
			fixture.Connection.GetSession()
				.GetRegistrySnapshot().get(),
			snapshot.get());

		EXPECT_EQ(
			fixture.Connection.GetSession()
				.GetConfigurationGeneration(),
			9u);
	}

	TEST(
		ConfigurationRuntimeTests,
		RejectedCompletionClosesAfterFlush) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		auto data =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(data.HasError());

		auto finish =
			fixture.Connection.Receive(
				MakeFinishConfiguration().Bytes());

		ASSERT_FALSE(finish.HasError());

		const auto complete =
			GetRequest<ConfigurationCompleteRequest>(
				finish.Requests.front());

		auto rejected =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationRejectedResolution{
						.Id = complete.Id,
					}});

		ASSERT_FALSE(rejected.HasError());

		EXPECT_EQ(
			rejected.Disposition,
			ConnectionDisposition::CloseAfterFlush);

		EXPECT_TRUE(
			fixture.Connection.GetSession()
				.IsDisconnected());
	}

	TEST(
		ConfigurationRuntimeTests,
		PinnedGenerationSurvivesNewerPublication) {

		RegistrySnapshotStore store;

		auto first =
			BuildSyntheticSnapshot(1);

		ASSERT_TRUE(store.Publish(first).has_value());

		auto fixture = EnterConfiguration();

		auto plan =
			ConfigurationSequenceBuilder::Build(
				store.GetActiveSnapshot(),
				MakePolicy());

		ASSERT_TRUE(plan.has_value());

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationReadyResolution{
						.Id = fixture.StartRequest.Id,
						.Plan = std::move(*plan),
					}});

		ASSERT_FALSE(bootstrap.HasError());

		auto second =
			BuildSyntheticSnapshot(2);

		ASSERT_TRUE(store.Publish(second).has_value());
		EXPECT_EQ(store.GetActiveGeneration(), 2u);

		auto data =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(data.HasError());

		auto finish =
			fixture.Connection.Receive(
				MakeFinishConfiguration().Bytes());

		ASSERT_FALSE(finish.HasError());

		const auto complete =
			GetRequest<ConfigurationCompleteRequest>(
				finish.Requests.front());

		EXPECT_EQ(complete.Generation, 1u);

		auto accepted =
			fixture.Connection.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationAcceptedResolution{
						.Id = complete.Id,
					}});

		ASSERT_FALSE(accepted.HasError());

		EXPECT_EQ(
			fixture.Connection.GetSession()
				.GetConfigurationGeneration(),
			1u);

		EXPECT_EQ(
			fixture.Connection.GetSession()
				.GetRegistrySnapshot().get(),
			first.get());
	}

	TEST(
		ConfigurationRuntimeTests,
		FragmentedKnownPackSelectionIsBuffered) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		const auto selection =
			MakeKnownPackSelection();

		ASSERT_GT(selection.Size(), 1u);

		for (std::size_t index{ 0 };
			index + 1 < selection.Size();
			++index) {

			auto partial =
				fixture.Connection.Receive(
					selection.Bytes().subspan(index, 1));

			EXPECT_FALSE(partial.HasError());
			EXPECT_TRUE(partial.OutboundFrames.empty());
		}

		auto final =
			fixture.Connection.Receive(
				selection.Bytes().last(1));

		ASSERT_FALSE(final.HasError());
		ASSERT_EQ(final.OutboundFrames.size(), 1u);
	}

	TEST(
		ConfigurationRuntimeTests,
		ClientLifecycleTracksConfigurationAndPlayTransitions) {

		const auto started_at =
			ClientConnection::TimePoint{};

		ClientConnection client{
			ConnectionId{ 44 },
			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 25565,
			},
			NetworkEndpoint{
				.Address = "127.0.0.1",
				.Port = 50044,
			},
			{},
			{},
			started_at
		};

		const auto login_input = Combine(
			MakeLoginHandshake().Bytes(),
			MakeLoginStart("PlayerOne").Bytes());

		auto login_start =
			client.Receive(
				login_input,
				started_at
					+ std::chrono::seconds{ 1 });

		ASSERT_FALSE(login_start.HasError());
		ASSERT_EQ(login_start.Requests.size(), 1u);

		const auto login_request =
			GetRequest<LoginStartRequest>(
				login_start.Requests.front());

		auto login_resolution =
			client.ResolveLogin(
				LoginAcceptedResolution{
					.Id = login_request.Id,
					.ProfileId =
						Uuid::FromOfflinePlayerName(
							"PlayerOne"),
					.Username = "PlayerOne",
					.Properties = {},
				},
				started_at
					+ std::chrono::seconds{ 2 });

		ASSERT_FALSE(login_resolution.HasError());

		auto acknowledged =
			client.Receive(
				MakeLoginAcknowledged().Bytes(),
				started_at
					+ std::chrono::seconds{ 3 });

		ASSERT_FALSE(acknowledged.HasError());
		ASSERT_EQ(acknowledged.Requests.size(), 1u);

		EXPECT_EQ(
			client.GetLifecycleState(),
			ClientLifecycleState::Configuration);

		const auto start_request =
			GetRequest<ConfigurationStartRequest>(
				acknowledged.Requests.front());

		auto ready =
			client.ResolveConfiguration(
				BuildReadyResolution(
					start_request.Id,
					5),
				started_at
					+ std::chrono::seconds{ 4 });

		ASSERT_FALSE(ready.HasError());

		auto selected =
			client.Receive(
				MakeKnownPackSelection().Bytes(),
				started_at
					+ std::chrono::seconds{ 5 });

		ASSERT_FALSE(selected.HasError());

		auto finished =
			client.Receive(
				MakeFinishConfiguration().Bytes(),
				started_at
					+ std::chrono::seconds{ 6 });

		ASSERT_FALSE(finished.HasError());
		ASSERT_EQ(finished.Requests.size(), 1u);

		const auto complete_request =
			GetRequest<ConfigurationCompleteRequest>(
				finished.Requests.front());

		auto accepted =
			client.ResolveConfiguration(
				ConfigurationResolution{
					ConfigurationAcceptedResolution{
						.Id = complete_request.Id,
					}},
				started_at
					+ std::chrono::seconds{ 7 });

		ASSERT_FALSE(accepted.HasError());

		EXPECT_EQ(
			client.GetLifecycleState(),
			ClientLifecycleState::Play);

		EXPECT_EQ(
			client.GetProtocolState(),
			ProtocolState::Play);

		EXPECT_EQ(
			client.GetStateEnteredAt(),
			started_at
				+ std::chrono::seconds{ 7 });
	}

	TEST(
		ConfigurationRuntimeTests,
		ClientBrandDoesNotAdvanceConfigurationSequence) {

		auto fixture = EnterConfiguration();

		auto brand =
			fixture.Connection.Receive(
				MakeClientBrand().Bytes());

		ASSERT_FALSE(brand.HasError());
		EXPECT_TRUE(brand.OutboundFrames.empty());
		EXPECT_TRUE(brand.Requests.empty());

		const auto& retained_brand =
			fixture.Connection.GetSession()
			.GetClientBrand();

		ASSERT_TRUE(retained_brand.has_value());
		EXPECT_EQ(*retained_brand, "vanilla");

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		auto selection =
			fixture.Connection.Receive(
				MakeKnownPackSelection().Bytes());

		ASSERT_FALSE(selection.HasError());
		ASSERT_EQ(
			selection.OutboundFrames.size(),
			1u);
	}

	TEST(
		ProtocolConnectionDiagnosticsTests,
		ReportsUnsupportedConfigurationPacketContext) {

		auto fixture = EnterConfiguration();

		auto bootstrap =
			fixture.Connection.ResolveConfiguration(
				BuildReadyResolution(
					fixture.StartRequest.Id));

		ASSERT_FALSE(bootstrap.HasError());

		const auto unsupported =
			EncodePacketFrame(
				Configuration::Serverbound::
				KeepAlivePacketId,
				std::span<const std::byte>{});

		const auto result =
			fixture.Connection.Receive(
				unsupported.Bytes());

		ASSERT_TRUE(result.HasError());
		ASSERT_TRUE(
			result.FailureContext.has_value());

		const auto& context =
			*result.FailureContext;

		EXPECT_EQ(context.Stage, ProtocolProcessingStage::HandlePacket);

		EXPECT_EQ(
			context.State,
			ProtocolState::Configuration);

		ASSERT_TRUE(context.PacketId.has_value());

		EXPECT_EQ(
			*context.PacketId,
			Configuration::Serverbound::
			KeepAlivePacketId);

		EXPECT_EQ(
			context.PacketPayloadBytes,
			0u);

		ASSERT_TRUE(
			std::holds_alternative<
			ProtocolError>(*result.Error));

		EXPECT_EQ(
			std::get<ProtocolError>(
				*result.Error),
			ProtocolError::UnexpectedPacket);
	}
}

