#pragma once

#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <sonnet/sonnet.hpp>

namespace Aurore::Core {
	struct ConfigResult {
		bool Success{ false };
		std::string Error;

		operator bool() const { return Success; }
	};

	class Configuration {
	public:
		Configuration() = default;

		void CreateDefaults();

		ConfigResult Load(const std::filesystem::path& path);
		ConfigResult Save(const std::filesystem::path& path) const;

		bool Exists(std::string_view key) const;

		template<typename T>
		std::optional<T> Get(std::string_view key) const;

		template<typename T>
		T GetOr(std::string_view key, T default_value) const;

		template<typename T>
		void Set(std::string_view key, T value);

	private:
		Sonnet::value* GetOrCreate(std::string_view key);
		Sonnet::value* Find(std::string_view key);
		const Sonnet::value* Find(std::string_view key) const;

		Sonnet::value m_Data;
		std::filesystem::path m_Path;
	};

	template<typename T>
	std::optional<T> Configuration::Get(std::string_view key) const {
		const Sonnet::value* value{ Find(key) };
		if (!value) return std::nullopt;

		if constexpr (std::is_same_v<T, bool>) {
			if (!value->is_bool()) return std::nullopt;
			return value->as_bool();
		}
		else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
			if (!value->is_number()) return std::nullopt;

			const long double number{ static_cast<long double>(value->as_number()) };
			if (!std::isfinite(number)) return std::nullopt;
			if (std::trunc(number) != number) return std::nullopt;

			/*
				numeric_limits<T>::digits excludes the sign bit.

				Signed range:
					[-2^digits, 2^digits)

				Unsigned range:
					[0, 2^digits)
			*/
			const long double upper_exclusive{ std::ldexp(1.0L, std::numeric_limits<T>::digits) };
			const long double lower_inclusive{ std::is_signed_v<T> ? -upper_exclusive : 0.0L };
			if (number < lower_inclusive || number >= upper_exclusive) return std::nullopt;
			return static_cast<T>(number);
		}
		else if constexpr (std::is_floating_point_v<T>) {
			if (!value->is_number()) return std::nullopt;
			const long double number{ static_cast<long double>(value->as_number()) };
			if (!std::isfinite(number)) return std::nullopt;

			const long double minimum{ static_cast<long double>(std::numeric_limits<T>::lowest()) };
			const long double maximum{ static_cast<long double>(std::numeric_limits<T>::max()) };
			if (number < minimum || number > maximum) return std::nullopt;
			return static_cast<T>(number);
		}
		else if constexpr (std::is_same_v<T, std::string>) {
			if (!value->is_string()) return std::nullopt;
			return std::string(value->as_string());
		}
		else {
			static_assert(std::is_same_v<T, void>, "Unsupported configuration type!");
		}
	}

	template<typename T>
	T Configuration::GetOr(std::string_view key, T default_value) const {
		auto result = Get<T>(key);
		if (result.has_value()) return *result;
		return default_value;
	}

	template<typename T>
	void Configuration::Set(std::string_view key, T v) {
		Sonnet::value* target = GetOrCreate(key);
		*target = v;
	}
} // namespace Aurore::Core
