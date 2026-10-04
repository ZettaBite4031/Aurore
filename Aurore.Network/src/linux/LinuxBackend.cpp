#include "LinuxBackend.hpp"

#include <cerrno>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <sys/epoll.h>

namespace Aurore::Network::Detail {
    std::unique_ptr<NetworkBackend> CreateLinuxEpollBackend() {
        return std::make_unique<Linux::LinuxNetworkBackend>();
    }
}

namespace Aurore::Network::Detail::Linux {
    LinuxNetworkBackend::~LinuxNetworkBackend() noexcept {
        Shutdown();
    }

    NetworkResult<void> LinuxNetworkBackend::Initialize(const NetworkConfiguration& config, NetworkCommandQueue& commands, NetworkEventQueue& events, NetworkResourceLedger& resources) {
        if (m_Initialized) {
            return std::unexpected(NetworkError::AlreadyInitialized);
        }

        UniqueFd epoll = CreateEpollInstance();
        if (!epoll) {
            return std::unexpected(NetworkError::BackendFailure);
        }

        UniqueFd command_event = CreateEventCounter();
        if (!command_event) {
            return std::unexpected(NetworkError::BackendFailure);
        }

        if (!AddEpollInterest(epoll.Get(), command_event.Get(), EPOLLIN)) {
            return std::unexpected(NetworkError::BackendFailure);
        }

        m_Config = config;
        m_Commands = &commands;
        m_Events = &events;
        m_Resources = &resources;

        m_Epoll = std::move(epoll);
        m_CommandEvent = std::move(command_event);

        m_StopRequested.store(false, std::memory_order_release);
        m_Started.store(false, std::memory_order_release);

        m_WorkerFailureReported = false;
        m_Initialized = true;

        return {};
    }

    NetworkResult<NetworkEndpoint> LinuxNetworkBackend::Start() {
        if (!m_Initialized) {
            return std::unexpected(NetworkError::NotInitialized);
        }

        if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable()) {
            return std::unexpected(NetworkError::AlreadyRunning);
        }

        /*
         * The epoll lifecycle is ready, but Start() cannot truthfully
         * succeed until step 2 creates the listener and can return its
         * actual bound endpoint
         *
         * Step 2 will:
         *   1. create/register the listening socket;
         *   2. call StartWorker();
         *   3. return the bound NetworkEndpoint.
         */
        return std::unexpected(NetworkError::BackendUnavailable);
    }

    void LinuxNetworkBackend::NotifyCommandAvailable() noexcept {
        if (!m_Started.load(std::memory_order_acquire)) {
            return;
        }

        if (WakeEventCounter(m_CommandEvent.Get())) {
            return;
        }

        m_StopRequested.store(true, std::memory_order_release);
    }

    void LinuxNetworkBackend::Stop() noexcept {
        StopWorker();
    }

    void LinuxNetworkBackend::Shutdown() noexcept {
        StopWorker();

        m_CommandEvent.Reset();
        m_Epoll.Reset();

        m_Commands = nullptr;
        m_Events = nullptr;
        m_Resources = nullptr;

        m_Config = {};
        m_Initialized = false;
        m_WorkerFailureReported = false;

        m_StopRequested.store(false, std::memory_order_release);
        m_Started.store(false, std::memory_order_release);

        {
            std::scoped_lock lock(m_StartupMutex);
            m_StartupComplete = false;
            m_StartupError.reset();
        }
    }

    NetworkResult<void> LinuxNetworkBackend::StartWorker() {
        if (!m_Initialized) {
            return std::unexpected(NetworkError::NotInitialized);
        }

        if (m_Started.load(std::memory_order_acquire) || m_Worker.joinable()) {
            return std::unexpected(NetworkError::AlreadyRunning);
        }

        if (!m_Epoll || !m_CommandEvent) {
            return std::unexpected(NetworkError::BackendFailure);
        }

        {
            std::scoped_lock lock(m_StartupMutex);

            m_StartupComplete = false;
            m_StartupError.reset();
        }

        m_StopRequested.store(false, std::memory_order_release);

        m_WorkerFailureReported = false;

        m_Started.store(true, std::memory_order_release);

        try {
            m_Worker = std::thread(&LinuxNetworkBackend::WorkerMain, this);
        } catch (...) {
            m_Started.store(false, std::memory_order_release);
            return std::unexpected(NetworkError::BackendFailure);
        }

        std::optional<NetworkError> startup_error;

#ifndef NDEBUG
        bool startup_timed_out{ false };
#endif

        {
            std::unique_lock lock(m_StartupMutex);

#ifndef NDEBUG
            constexpr auto StartupTimeout = std::chrono::seconds{ 5 };
            const bool completed = m_StartupCondition.wait_for(lock, StartupTimeout, [this] { return m_StartupComplete; });
            if (!completed) {
                startup_error = NetworkError::BackendFailure;
                startup_timed_out = true;
            } else {
                startup_error = m_StartupError;
            }
#else
            m_StartupCondition.wait(lock, [this] { return m_StartupComplete; });
            startup_error = m_StartupError;
#endif
        }

#ifndef NDEBUG
        if (startup_timed_out) {
            m_StopRequested.store(true, std::memory_order_release);
            [[maybe_unused]] const bool woke = WakeEventCounter(m_CommandEvent.Get());
        }
#endif

        if (startup_error.has_value()) {
            if (m_Worker.joinable()) m_Worker.join();

            m_Started.store(false, std::memory_order_release);
            return std::unexpected(*startup_error);
        }

        return {};
    }

    void LinuxNetworkBackend::StopWorker() noexcept {
        if (!m_Worker.joinable()) {
            m_Started.store(false, std::memory_order_release);
            return;
        }

        m_StopRequested.store(true, std::memory_order_release);

        [[maybe_unused]] const bool woke = WakeEventCounter(m_CommandEvent.Get());

        m_Worker.join();
        m_Started.store(false, std::memory_order_release);
    }

    void LinuxNetworkBackend::WorkerMain() noexcept {
        SignalStartup(std::nullopt);

        WorkerEventBuffer events{};

        while (!m_StopRequested.load(std::memory_order_acquire)) {
            const int count = ::epoll_wait(m_Epoll.Get(), events.data(), static_cast<int>(events.size()), WaitIndefinitely);
            if (count < 0) {
                const int error = errno;
                if (error == EINTR) continue;
                HandleWorkerFailure(error, "epoll_wait");
                break;
            }

            for (int i = 0; i < count; i++) {
                const epoll_event& event = events[static_cast<std::size_t>(i)];
                if (event.data.fd == m_CommandEvent.Get()) {
                    if (!DrainEventCounter(m_CommandEvent.Get())) {
                        HandleWorkerFailure(errno, "eventfd read");
                        break;
                    }

                    /*
                     * Command dispatch intentional arrives with
                     * the send/receive step. For this lifecycle
                     * milstone the eventfd is the only worker
                     * wake/stop channel.
                     */
                    continue;
                }

                /*
                 * No listener or client descriptors are registered
                 * during step 1. Seeing any other descriptor here
                 * means the event loop state is inconsistent.
                 */
                HandleWorkerFailure(EINVAL, "unexpected epoll source");
                break;
            }
        }

        SignalStartupFailureNoexcept();

        m_Started.store(false, std::memory_order_release);
    }

    void LinuxNetworkBackend::HandleWorkerFailure(int error, std::string_view operation) noexcept {
        m_StopRequested.store(true, std::memory_order_release);

        SignalStartupFailureNoexcept();

        if (m_WorkerFailureReported) return;

        m_WorkerFailureReported = true;

        try {
            std::string message{ "Linux epoll worker failure in " };
            message.append(operation);

            if (error != 0) {
                message += ": ";
                message += FormatSystemError(error);
            }

            EmitFailure(NetworkError::BackendFailure, std::move(message), true);
        } catch (...) {
            /*
             * Worker failure reporting is best-effort.
             * The original failure must remain contained.
             */
        }
    }

    void LinuxNetworkBackend::EmitFailure(NetworkError error, std::string message, bool fatal) noexcept {
        if (m_Events == nullptr) return;
        try {
            [[maybe_unused]] const auto result = m_Events->Push(QueuedNetworkEvent{ .Event = NetworkFailureEvent{ .Error = error, .Message = std::move(message), .Fatal = fatal, }, .InboundReservation = {}, });
        } catch (...) {
            /*
             * Failure reporting must not escape the network worker.
             */
        }
    }

    void LinuxNetworkBackend::SignalStartup(std::optional<NetworkError> error) noexcept {
        try {
            {
                std::scoped_lock lock(m_StartupMutex);
                if (m_StartupComplete) return;
                m_StartupError = error;
                m_StartupComplete = true;
            }
            m_StartupCondition.notify_all();
        } catch (...) {
            m_StopRequested.store(true, std::memory_order_release);
            m_StartupCondition.notify_all();
        }
    }

    void LinuxNetworkBackend::SignalStartupFailureNoexcept() noexcept {
        try {
            bool notify{ false };
            {
                std::scoped_lock lock(m_StartupMutex);
                if (!m_StartupComplete) {
                    m_StartupError = NetworkError::BackendFailure;
                    m_StartupComplete = true;
                    notify = true;
                }
            }

            if (notify) m_StartupCondition.notify_all();
        } catch (...) {
            m_StartupCondition.notify_all();
        }
    }

    bool LinuxNetworkBackend::IsStartupComplete() const noexcept {
        try {
            std::scoped_lock lock(m_StartupMutex);
            return m_StartupComplete;
        } catch (...) {
            return false;
        }
    }
}

