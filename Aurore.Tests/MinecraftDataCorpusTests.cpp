#include <Aurore/Data/MinecraftDataCorpus.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::MinecraftDataCorpusErrorCode;
		using Aurore::Data::MinecraftDataCorpusFactory;
		using Aurore::Data::RawDataPreparationResult;

		class MinecraftDataCorpusTests : public ::testing::Test {
		protected:
			void SetUp() override {
				m_Root = std::filesystem::temp_directory_path() / "aurore-minecraft-data-corpus-tests";
				std::error_code error;
				std::filesystem::remove_all(m_Root, error);
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

			[[nodiscard]] RawDataPreparationResult MakePreparation() const {
				return RawDataPreparationResult{
					.Version = { .Id = "1.21.11", .ProtocolVersion = 774 },
					.SourceSha1 = "0123456789abcdef0123456789abcdef01234567",
					.GenerationRoot = m_Root,
					.ReportsRoot = m_Root / "reports",
					.DataRoot = m_Root / "data",
				};
			}

			std::filesystem::path m_Root;
		};
	}

	TEST_F(MinecraftDataCorpusTests, CreatesValidatedCorpusFromPreparation) {
		const auto result = MinecraftDataCorpusFactory::FromPreparation(MakePreparation());
		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(result->Version.Id, "1.21.11");
		EXPECT_EQ(result->Version.ProtocolVersion, 774);
		EXPECT_EQ(result->SourceSha1, "0123456789abcdef0123456789abcdef01234567");
		EXPECT_EQ(result->ReportsRoot, m_Root / "reports");
		EXPECT_EQ(result->RegistryReport, m_Root / "reports" / "registries.json");
		EXPECT_EQ(result->DataRoot, m_Root / "data");
		EXPECT_EQ(result->MinecraftDataRoot, m_Root / "data" / "minecraft");
	}

	TEST_F(MinecraftDataCorpusTests, RejectsInvalidSourceIdentity) {
		auto preparation = MakePreparation();
		preparation.SourceSha1 = "bad";
		const auto result = MinecraftDataCorpusFactory::FromPreparation(preparation);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, MinecraftDataCorpusErrorCode::InvalidSourceIdentity);
	}

	TEST_F(MinecraftDataCorpusTests, RejectsMissingRegistryReport) {
		std::filesystem::remove(m_Root / "reports" / "registries.json");
		const auto result = MinecraftDataCorpusFactory::FromPreparation(MakePreparation());
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, MinecraftDataCorpusErrorCode::MissingRegistryReport);
	}

	TEST_F(MinecraftDataCorpusTests, RejectsMissingMinecraftData) {
		std::filesystem::remove_all(m_Root / "data" / "minecraft");
		const auto result = MinecraftDataCorpusFactory::FromPreparation(MakePreparation());
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().Code, MinecraftDataCorpusErrorCode::MissingMinecraftData);
	}
}
