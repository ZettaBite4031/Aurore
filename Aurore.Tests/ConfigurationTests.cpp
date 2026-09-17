#include <Aurore/Core/Configuration.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace Aurore::Tests {
	TEST(ConfigurationTests, CreatesNetworkPortUnderCanonicalKey) {
		Aurore::Core::Configuration config;
		config.CreateDefaults();

		const auto port = config.Get<std::uint16_t>("Network.Port");

		ASSERT_TRUE(port.has_value());
		EXPECT_EQ(*port, 25565);
		EXPECT_FALSE(config.Exists("Server.Port"));
	}

	TEST(ConfigurationTests, RejectsInvalidIntegralConversions) {
		Aurore::Core::Configuration configuration;

		configuration.Set("Test.NegativeUnsigned", -1);
		configuration.Set("Test.FractionalInteger", 25565.5);
		configuration.Set("Test.PortOverflow", 65536);
		configuration.Set("Test.ValidPort", 25565);

		EXPECT_FALSE(
			configuration.Get<std::uint16_t>(
				"Test.NegativeUnsigned").has_value());

		EXPECT_FALSE(
			configuration.Get<std::uint16_t>(
				"Test.FractionalInteger").has_value());

		EXPECT_FALSE(
			configuration.Get<std::uint16_t>(
				"Test.PortOverflow").has_value());

		const auto valid_port =
			configuration.Get<std::uint16_t>("Test.ValidPort");

		ASSERT_TRUE(valid_port.has_value());
		EXPECT_EQ(*valid_port, 25565);
	}
}
