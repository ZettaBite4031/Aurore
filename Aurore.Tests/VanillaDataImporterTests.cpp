#include <Aurore/Data/MinecraftDataCorpus.hpp>
#include <Aurore/Data/VanillaDataImporter.hpp>

#include <Aurore/Util/Registry.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::MinecraftDataCorpus;
		using Aurore::Data::MinecraftDataCorpusFactory;
		using Aurore::Data::RawDataPreparationResult;
		using Aurore::Data::VanillaDataImporter;
		using Aurore::Data::VanillaDataImportErrorCode;
		using Aurore::Util::RegistryGeneration;

		class VanillaDataImporterTests : public ::testing::Test {
		protected:
			void SetUp() override {
				m_Root = std::filesystem::temp_directory_path() / "aurore-vanilla-data-importer-tests";

				std::error_code error;
				std::filesystem::remove_all(m_Root, error);

				error.clear();
				std::filesystem::create_directories(m_Root / "reports", error);
				ASSERT_FALSE(error);

				std::filesystem::create_directories(m_Root / "data" / "minecraft", error);
				ASSERT_FALSE(error);

				std::ofstream{ m_Root / "reports" / "registries.json" } << "{}";
			}

			void TearDown() override {
				std::error_code error;
				std::filesystem::remove_all(m_Root, error);
			}

			[[nodiscard]] MinecraftDataCorpus MakeCorpus(
				std::string_view version = "1.21.11",
				std::int32_t protocol_version = 774) const {

				const auto result = MinecraftDataCorpusFactory::FromPreparation(
					RawDataPreparationResult{
						.Version = {
							.Id = std::string{ version },
							.ProtocolVersion = protocol_version,
						},
						.SourceSha1 = "0123456789abcdef0123456789abcdef01234567",
						.GenerationRoot = m_Root,
						.ReportsRoot = m_Root / "reports",
						.DataRoot = m_Root / "data",
					});

				EXPECT_TRUE(result.has_value());
				return result.value();
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(VanillaDataImporterTests, BuildsSnapshotForSupportedTarget) {
		const RegistryGeneration generation{ 17 };
		const auto result = VanillaDataImporter::Import(MakeCorpus(), generation);

		ASSERT_TRUE(result.has_value());
		ASSERT_NE(*result, nullptr);
		EXPECT_EQ((*result)->GetGeneration(), generation);
		EXPECT_TRUE((*result)->GetBlocks().Empty());
		EXPECT_TRUE((*result)->GetItems().Empty());
		EXPECT_TRUE((*result)->GetBiomes().Empty());
		EXPECT_TRUE((*result)->GetDimensionTypes().Empty());
		EXPECT_TRUE((*result)->GetNetworkRegistries().Empty());
	}

	TEST_F(VanillaDataImporterTests, RejectsUnsupportedMinecraftVersion) {
		const auto result = VanillaDataImporter::Import(MakeCorpus("1.21.10", 774), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::UnsupportedMinecraftVersion);
		EXPECT_EQ(result.error().MinecraftVersion, "1.21.10");
		EXPECT_EQ(result.error().ProtocolVersion, 774);
		EXPECT_EQ(result.error().Generation, 1u);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}

	TEST_F(VanillaDataImporterTests, RejectsUnsupportedProtocolVersion) {
		const auto result = VanillaDataImporter::Import(MakeCorpus("1.21.11", 773), 1);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::UnsupportedProtocolVersion);
		EXPECT_EQ(result.error().MinecraftVersion, "1.21.11");
		EXPECT_EQ(result.error().ProtocolVersion, 773);
		EXPECT_EQ(result.error().Generation, 1u);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}

	TEST_F(VanillaDataImporterTests, RejectsInvalidGeneration) {
		const auto result = VanillaDataImporter::Import(
			MakeCorpus(),
			Aurore::Util::NoRegistryGeneration);

		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, VanillaDataImportErrorCode::InvalidGeneration);
		EXPECT_EQ(result.error().Generation, Aurore::Util::NoRegistryGeneration);
		EXPECT_FALSE(result.error().SnapshotError.has_value());
	}
}
