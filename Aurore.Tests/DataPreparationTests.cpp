#include <Aurore/Data/DataPreparation.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <utility>
#include <variant>

namespace Aurore::Tests {
	namespace {
		using Aurore::Data::DataPreparationErrorCode;
		using Aurore::Data::DataPreparationOptions;
		using Aurore::Data::DataPreparationRequest;
		using Aurore::Data::LocalJarSource;
		using Aurore::Data::MinecraftVersion;
		using Aurore::Data::OfficialDownloadSource;

		DataPreparationOptions MakeOptions() {
			return DataPreparationOptions{
				.Version = MinecraftVersion{
					.Id = "1.21.11",
					.ProtocolVersion = 774,
				},
				.Source = OfficialDownloadSource{},
				.CacheRoot = "cache",
			};
		}
	}

	TEST(
		DataPreparationRequestTests,
		AcceptsOfficialDownloadSource) {

		auto options = MakeOptions();

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_TRUE(result.has_value());

		EXPECT_EQ(
			result->GetVersion().Id,
			"1.21.11");

		EXPECT_EQ(
			result->GetVersion().ProtocolVersion,
			774);

		EXPECT_TRUE(
			std::holds_alternative<
			OfficialDownloadSource>(
				result->GetSource()));
	}

	TEST(
		DataPreparationRequestTests,
		AcceptsLocalJarSource) {

		auto options = MakeOptions();

		options.Source = LocalJarSource{
			.JarPath = "server.jar",
		};

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_TRUE(result.has_value());

		const auto* source =
			std::get_if<LocalJarSource>(
				&result->GetSource());

		ASSERT_NE(source, nullptr);

		EXPECT_EQ(
			source->JarPath,
			std::filesystem::path{
				"server.jar"
			});
	}

	TEST(
		DataPreparationRequestTests,
		RejectsEmptyMinecraftVersion) {

		auto options = MakeOptions();
		options.Version.Id.clear();

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			DataPreparationErrorCode::
			EmptyMinecraftVersion);
	}

	TEST(
		DataPreparationRequestTests,
		RejectsInvalidProtocolVersion) {

		auto options = MakeOptions();
		options.Version.ProtocolVersion = 0;

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			DataPreparationErrorCode::
			InvalidProtocolVersion);
	}

	TEST(
		DataPreparationRequestTests,
		RejectsEmptyCacheRoot) {

		auto options = MakeOptions();
		options.CacheRoot.clear();

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			DataPreparationErrorCode::
			EmptyCacheRoot);
	}

	TEST(
		DataPreparationRequestTests,
		RejectsEmptyLocalJarPath) {

		auto options = MakeOptions();

		options.Source = LocalJarSource{};

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			DataPreparationErrorCode::
			EmptyLocalJarPath);
	}

	TEST(
		DataPreparationRequestTests,
		RejectsMissingSource) {

		auto options = MakeOptions();
		options.Source = std::monostate{};

		const auto result =
			DataPreparationRequest::Create(
				std::move(options));

		ASSERT_FALSE(result.has_value());

		EXPECT_EQ(
			result.error().Code,
			DataPreparationErrorCode::
			MissingSource);
	}
}
