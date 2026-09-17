#pragma once

#include "WindowsBackend.hpp"

#include <memory>
#include <mutex>
#include <optional>

namespace Aurore::Network::Detail::Windows {
	class WindowsNetworkBackendTestAccess final {
	public:
		[[nodiscard]] static bool ProcessCompletion(WindowsNetworkBackend& backend, bool succeeded, DWORD transferred_bytes, CompletionKey completion_key, OVERLAPPED* overlapped, DWORD error) {
			const auto result = backend.ProcessCompletion(WindowsNetworkBackend::WorkerCompletion{
				.Succeeded = succeeded,
				.TransferredBytes = transferred_bytes,
				.CompletionKey = static_cast<ULONG_PTR>(completion_key),
				.Overlapped = overlapped,
				.Error = error,
			});
			return result == WindowsNetworkBackend::WorkerStepResult::Continue;
		}

		[[nodiscard]] static OVERLAPPED* AddAcceptOperation(WindowsNetworkBackend& backend) {
			auto operation = std::make_unique<AcceptOperation>();
			auto* overlapped = &operation->Overlapped;
			const auto [iterator, inserted] = backend.m_Operations.emplace(overlapped, std::move(operation));
			(void)iterator;
			return inserted ? overlapped : nullptr;
		}

		[[nodiscard]] static std::size_t OperationCount(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_Operations.size();
		}

		[[nodiscard]] static std::size_t ConnectionCount(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_Connections.size();
		}

		[[nodiscard]] static std::size_t QueuedOutboundBytes(const WindowsNetworkBackend& backend, ConnectionId connection) noexcept {
			const auto iterator = backend.m_Connections.find(connection);
			if (iterator == backend.m_Connections.end()) return 0;
			std::size_t bytes{ 0 };
			for (const auto& outbound : iterator->second.OutboundQueue) bytes += outbound.OutboundReservation.RemainingBytes();
			return bytes;
		}

		[[nodiscard]] static std::size_t ActiveSendReservationBytes(const WindowsNetworkBackend& backend) noexcept {
			std::size_t bytes{ 0 };
			for (const auto& [overlapped, operation] : backend.m_Operations) {
				(void)overlapped;
				if (operation != nullptr && operation->Kind == IoOperationKind::Send) bytes += static_cast<const SendOperation*>(operation.get())->OutboundReservation.RemainingBytes();
			}
			return bytes;
		}

		[[nodiscard]] static NetworkResourceSnapshot GetResourceSnapshot(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_Resources != nullptr ? backend.m_Resources->GetSnapshot() : NetworkResourceSnapshot{};
		}

		static void SetShuttingDown(WindowsNetworkBackend& backend, bool shutting_down) noexcept {
			backend.m_ShuttingDown = shutting_down;
		}

		[[nodiscard]] static bool IsShuttingDown(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_ShuttingDown;
		}

		[[nodiscard]] static bool IsStopRequested(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_StopRequested.load(std::memory_order_acquire);
		}

		[[nodiscard]] static bool WasWorkerFailureReported(const WindowsNetworkBackend& backend) noexcept {
			return backend.m_WorkerFailureReported;
		}

		static void HandleWorkerException(WindowsNetworkBackend& backend, std::string_view message) noexcept {
			backend.HandleWorkerException(message);
		}

		[[nodiscard]] static bool IsStartupComplete(WindowsNetworkBackend& backend) {
			std::scoped_lock lock(backend.m_StartupMutex);
			return backend.m_StartupComplete;
		}

		[[nodiscard]] static std::optional<NetworkError> GetStartupError(WindowsNetworkBackend& backend) {
			std::scoped_lock lock(backend.m_StartupMutex);
			return backend.m_StartupError;
		}
	};
}

