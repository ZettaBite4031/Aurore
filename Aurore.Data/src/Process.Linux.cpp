#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Process.hpp"

#include <Aurore/Build/Config.hpp>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static_assert(Aurore::Build::IsLinux, "Linux process backend compiled for non-Linux target.");

extern char** environ;

namespace {
	using Aurore::Data::Detail::ProcessError;
	using Aurore::Data::Detail::ProcessErrorCode;
	using Aurore::Data::Detail::ProcessRequest;
	using Aurore::Data::Detail::ProcessResult;

	class UniqueFd final {
	public:
		UniqueFd() noexcept = default;

		explicit UniqueFd(int fd) noexcept
			: m_Fd(fd) {}

		~UniqueFd() {
			Reset();
		}

		UniqueFd(const UniqueFd&) = delete;
		UniqueFd& operator=(
			const UniqueFd&) = delete;

		UniqueFd(UniqueFd&& other) noexcept
			: m_Fd(
				std::exchange(
					other.m_Fd,
					-1)) {}

		UniqueFd& operator=(
			UniqueFd&& other) noexcept {

			if (this == &other)
				return *this;

			Reset();

			m_Fd =
				std::exchange(
					other.m_Fd,
					-1);

			return *this;
		}

		[[nodiscard]]
		int Get() const noexcept {
			return m_Fd;
		}

		[[nodiscard]]
		explicit operator bool() const noexcept {
			return m_Fd >= 0;
		}

		void Reset(int fd = -1) noexcept {
			if (m_Fd >= 0)
				::close(m_Fd);

			m_Fd = fd;
		}

	private:
		int m_Fd{ -1 };
	};

	struct Pipe final {
		UniqueFd Read;
		UniqueFd Write;
	};

	struct StreamState final {
		UniqueFd* PipeFd{ nullptr };
		std::string* Output{ nullptr };

		std::size_t Limit{ 0 };
		std::size_t ObservedBytes{ 0 };

		bool LimitExceeded{ false };
		bool Open{ true };
	};

	[[nodiscard]]
	std::error_code PosixError(
		int value) noexcept {

		return std::error_code{
			value,
			std::generic_category()
		};
	}

	[[nodiscard]]
	ProcessError MakeError(
		ProcessErrorCode code,
		std::error_code system_error = {},
		std::size_t observed = 0,
		std::size_t limit = 0) {

		return ProcessError{
			.Code = code,
			.SystemError = system_error,
			.ObservedBytes = observed,
			.LimitBytes = limit,
		};
	}

	[[nodiscard]]
	std::expected<Pipe, ProcessError>
		CreatePipePair() {
		int handles[2]{ -1, -1 };

		if (::pipe2(
			handles,
			O_CLOEXEC) != 0) {

			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					PipeCreationFailed,
					PosixError(errno)));
		}

		return Pipe{
			.Read = UniqueFd{
				handles[0]
			},
			.Write = UniqueFd{
				handles[1]
			},
		};
	}

	[[nodiscard]]
	bool MoveAboveStandardDescriptors(
		UniqueFd& fd,
		std::error_code& error) {

		if (fd.Get() >
			STDERR_FILENO) {

			return true;
		}

		const int duplicate =
			::fcntl(
				fd.Get(),
				F_DUPFD_CLOEXEC,
				STDERR_FILENO + 1);

		if (duplicate < 0) {
			error = PosixError(errno);
			return false;
		}

		fd.Reset(duplicate);
		return true;
	}

	void AddObservedBytes(
		StreamState& state,
		std::size_t bytes) noexcept {

		if (bytes
		> std::numeric_limits<
			std::size_t>::max()
			- state.ObservedBytes) {

			state.ObservedBytes =
				std::numeric_limits<
				std::size_t>::max();

			return;
		}

		state.ObservedBytes += bytes;
	}

	[[nodiscard]]
	std::optional<ProcessError>
		ReadStream(StreamState& state) {
		std::array<char, 16 * 1024>
			buffer{};

		ssize_t count{ -1 };

		do {
			count =
				::read(
					state.PipeFd->Get(),
					buffer.data(),
					buffer.size());
		} while (count < 0
			&& errno == EINTR);

		if (count < 0) {
			return MakeError(
				ProcessErrorCode::
				OutputReadFailed,
				PosixError(errno));
		}

		if (count == 0) {
			state.PipeFd->Reset();
			state.Open = false;
			return std::nullopt;
		}

		const auto size =
			static_cast<std::size_t>(
				count);

		AddObservedBytes(
			state,
			size);

		const std::size_t remaining =
			state.Output->size() >=
			state.Limit
			? 0
			: state.Limit
			- state.Output->size();

		const std::size_t copy_size =
			std::min(
				remaining,
				size);

		state.Output->append(
			buffer.data(),
			copy_size);

		if (copy_size < size)
			state.LimitExceeded = true;

		return std::nullopt;
	}

	void KillAndReap(pid_t process) noexcept {
		(void)::kill(
			process,
			SIGKILL);

		int status{ 0 };

		while (::waitpid(
			process,
			&status,
			0) < 0) {

			if (errno != EINTR)
				break;
		}
	}
}

namespace Aurore::Data::Detail {
	std::expected<
		ProcessResult,
		ProcessError>
		Process::Run(
			const ProcessRequest& request) {

		if (request.Executable.empty()) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					EmptyExecutable));
		}

		auto stdout_pipe =
			CreatePipePair();

		if (!stdout_pipe)
			return std::unexpected(
				stdout_pipe.error());

		auto stderr_pipe =
			CreatePipePair();

		if (!stderr_pipe)
			return std::unexpected(
				stderr_pipe.error());

		std::error_code descriptor_error;

		for (UniqueFd* fd : {
			&stdout_pipe->Read,
			&stdout_pipe->Write,
			&stderr_pipe->Read,
			&stderr_pipe->Write }) {

			if (MoveAboveStandardDescriptors(
				*fd,
				descriptor_error)) {

				continue;
			}

			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					PipeCreationFailed,
					descriptor_error));
		}

		posix_spawn_file_actions_t actions{};

		int action_result =
			::posix_spawn_file_actions_init(
				&actions);

		if (action_result != 0) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					ProcessCreationFailed,
					PosixError(
						action_result)));
		}

		const auto destroy_actions =
			[&] {
			::posix_spawn_file_actions_destroy(
				&actions);
			};

		const auto add_action =
			[&](int result)
			-> std::optional<ProcessError> {

			if (result == 0)
				return std::nullopt;

			return MakeError(
				ProcessErrorCode::
				ProcessCreationFailed,
				PosixError(result));
			};

		std::string working_directory;
		if (request.WorkingDirectory) {
			working_directory = request.WorkingDirectory->string();
			if (auto error = add_action(
				::posix_spawn_file_actions_addchdir_np(
					&actions,
					working_directory.c_str()))) {

				destroy_actions();
				return std::unexpected(*error);
			}
		}

		if (auto error = add_action(
			::posix_spawn_file_actions_addopen(
				&actions,
				STDIN_FILENO,
				"/dev/null",
				O_RDONLY,
				0))) {

			destroy_actions();
			return std::unexpected(*error);
		}

		if (auto error = add_action(
			::posix_spawn_file_actions_adddup2(
				&actions,
				stdout_pipe->Write.Get(),
				STDOUT_FILENO))) {

			destroy_actions();
			return std::unexpected(*error);
		}

		if (auto error = add_action(
			::posix_spawn_file_actions_adddup2(
				&actions,
				stderr_pipe->Write.Get(),
				STDERR_FILENO))) {

			destroy_actions();
			return std::unexpected(*error);
		}

		for (const int fd : {
			stdout_pipe->Read.Get(),
				stdout_pipe->Write.Get(),
				stderr_pipe->Read.Get(),
				stderr_pipe->Write.Get() }) {

			if (auto error = add_action(
				::posix_spawn_file_actions_addclose(
					&actions,
					fd))) {

				destroy_actions();
				return std::unexpected(*error);
			}
		}

		std::vector<std::string>
			argument_storage;

		argument_storage.reserve(
			request.Arguments.size() + 1);

		argument_storage.push_back(
			request.Executable.string());

		argument_storage.insert(
			argument_storage.end(),
			request.Arguments.begin(),
			request.Arguments.end());

		std::vector<char*> arguments;
		arguments.reserve(
			argument_storage.size() + 1);

		for (auto& argument :
			argument_storage) {

			arguments.push_back(
				argument.data());
		}

		arguments.push_back(nullptr);

		pid_t process{ 0 };

		const int spawn_result =
			::posix_spawnp(
				&process,
				request.Executable.c_str(),
				&actions,
				nullptr,
				arguments.data(),
				environ);

		destroy_actions();

		if (spawn_result != 0) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					ProcessCreationFailed,
					PosixError(
						spawn_result)));
		}

		/*
			The parent must discard its copies of the child write
			descriptors before waiting for EOF.
		*/
		stdout_pipe->Write.Reset();
		stderr_pipe->Write.Reset();

		ProcessResult result;

		StreamState stdout_state{
			.PipeFd = &stdout_pipe->Read,
			.Output = &result.StdOut,
			.Limit =
				request.MaximumStandardOutputBytes,
		};

		StreamState stderr_state{
			.PipeFd = &stderr_pipe->Read,
			.Output = &result.StdErr,
			.Limit =
				request.MaximumStandardErrorBytes,
		};

		while (stdout_state.Open
			|| stderr_state.Open) {

			pollfd descriptors[2]{
				{
					.fd = stdout_state.Open
						? stdout_pipe->Read.Get()
						: -1,
					.events = POLLIN,
					.revents = 0,
				},
				{
					.fd = stderr_state.Open
						? stderr_pipe->Read.Get()
						: -1,
					.events = POLLIN,
					.revents = 0,
				},
			};

			int poll_result{ -1 };

			do {
				poll_result =
					::poll(
						descriptors,
						2,
						-1);
			} while (poll_result < 0
				&& errno == EINTR);

			if (poll_result < 0) {
				const auto error =
					PosixError(errno);

				KillAndReap(process);

				return std::unexpected(
					MakeError(
						ProcessErrorCode::
						OutputReadFailed,
						error));
			}

			StreamState* states[]{
				&stdout_state,
				&stderr_state,
			};

			for (std::size_t index{ 0 };
				index < 2;
				++index) {

				auto& descriptor =
					descriptors[index];

				auto& state =
					*states[index];

				if (!state.Open)
					continue;

				if ((descriptor.revents
					& POLLNVAL) != 0) {

					KillAndReap(process);

					return std::unexpected(
						MakeError(
							ProcessErrorCode::
							OutputReadFailed,
							PosixError(
								EBADF)));
				}

				if ((descriptor.revents
					& (POLLIN
						| POLLHUP
						| POLLERR)) == 0) {

					continue;
				}

				auto read_error =
					ReadStream(state);

				if (read_error) {
					KillAndReap(process);
					return std::unexpected(
						*read_error);
				}
			}
		}

		int status{ 0 };
		pid_t waited{ -1 };

		do {
			waited =
				::waitpid(
					process,
					&status,
					0);
		} while (waited < 0
			&& errno == EINTR);

		if (waited < 0) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					WaitFailed,
					PosixError(errno)));
		}

		if (stdout_state.LimitExceeded) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					StandardOutputLimitExceeded,
					{},
					stdout_state.ObservedBytes,
					stdout_state.Limit));
		}

		if (stderr_state.LimitExceeded) {
			return std::unexpected(
				MakeError(
					ProcessErrorCode::
					StandardErrorLimitExceeded,
					{},
					stderr_state.ObservedBytes,
					stderr_state.Limit));
		}

		if (WIFEXITED(status)) {
			result.ExitCode =
				WEXITSTATUS(status);

			return result;
		}

		if (WIFSIGNALED(status)) {
			result.ExitCode = -1;

			result.TerminationSignal =
				WTERMSIG(status);

			return result;
		}

		return std::unexpected(
			MakeError(
				ProcessErrorCode::
				ExitCodeUnavailable));
	}
}
