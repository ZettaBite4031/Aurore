#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Aurore::Tests {
	class LoopbackClient final {
	public:
		static constexpr std::chrono::milliseconds DefaultTimeout{ 3000 };

		LoopbackClient();
		~LoopbackClient() noexcept;

		LoopbackClient(const LoopbackClient&) = delete;
		LoopbackClient& operator=(const LoopbackClient&) = delete;

		LoopbackClient(LoopbackClient&&) = delete;
		LoopbackClient& operator=(LoopbackClient&&) = delete;

		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsConnected() const noexcept;
		[[nodiscard]] int GetLastError() const noexcept;

		[[nodiscard]] bool Connect(
			std::string_view address,
			std::uint16_t port,
			std::chrono::milliseconds timeout = DefaultTimeout);

		[[nodiscard]] bool SendAll(
			std::span<const std::byte> data,
			std::chrono::milliseconds timeout = DefaultTimeout);

		/*
			Returns:
				nullopt      timeout or socket error
				empty vector orderly peer shutdown
				data vector  received bytes
		*/
		[[nodiscard]] std::optional<std::vector<std::byte>> ReceiveSome(
			std::size_t maximum_bytes,
			std::chrono::milliseconds timeout);

		[[nodiscard]] std::optional<std::vector<std::byte>> ReceiveExact(
			std::size_t size,
			std::chrono::milliseconds timeout = DefaultTimeout);

		[[nodiscard]] bool WaitForEof(
			std::chrono::milliseconds timeout = DefaultTimeout);

		[[nodiscard]] bool WaitForDisconnect(
			std::chrono::milliseconds timeout = DefaultTimeout);

		void Close() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
