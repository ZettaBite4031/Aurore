#include "Process.hpp"

#include <Aurore/Build/Config.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

static_assert(
	Aurore::Build::IsWindows,
	"Windows process backend compiled for non-Windows target.");

namespace {
	using Aurore::Data::Detail::ProcessError;
	using Aurore::Data::Detail::ProcessErrorCode;
	using Aurore::Data::Detail::ProcessRequest;
	using Aurore::Data::Detail::ProcessResult;

	class UniqueHandle final {
	public:
		UniqueHandle() noexcept = default;
		explicit UniqueHandle(HANDLE handle) noexcept
			: m_Handle(handle) {}

		~UniqueHandle() { Reset(); }

		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;

		UniqueHandle(UniqueHandle&& other) noexcept
			: m_Handle(std::exchange(other.m_Handle, nullptr)) {}

		UniqueHandle& operator=(UniqueHandle&& other) noexcept {
			if (this == &other) return *this;

			Reset();
			m_Handle = std::exchange(other.m_Handle, nullptr);
			return *this;
		}

		[[nodiscard]]
		HANDLE Get() const noexcept {
			return m_Handle;
		}

		[[nodiscard]]
		explicit operator bool() const noexcept {
			return m_Handle != nullptr && m_Handle != INVALID_HANDLE_VALUE;
		}

		void Reset(HANDLE handle = nullptr) noexcept {
			if (*this) ::CloseHandle(m_Handle);
			m_Handle = handle;
		}

	private:
		HANDLE m_Handle{ nullptr };
	};

	struct Pipe final {
		UniqueHandle Read;
		UniqueHandle Write;
	};

	struct PipeReadState final {
		std::error_code Error;
		std::size_t ObservedBytes{ 0 };
		bool LimitExceeded{ false };
	};

	[[nodiscard]]
	std::error_code WindowsError(DWORD value) noexcept {
		return std::error_code{ static_cast<int>(value), std::system_category() };
	}

	[[nodiscard]]
	ProcessError MakeError(ProcessErrorCode code, std::error_code system_error = {}, std::size_t observed = 0, std::size_t limit = 0) {
		return ProcessError{ .Code = code, .SystemError = system_error, .ObservedBytes = observed, .LimitBytes = limit, };
	}

	[[nodiscard]] std::expected<Pipe, ProcessError> CreatePipePair() {
		SECURITY_ATTRIBUTES attributes{
			.nLength = sizeof(SECURITY_ATTRIBUTES),
			.lpSecurityDescriptor = nullptr,
			.bInheritHandle = TRUE,
		};

		HANDLE read_handle{ nullptr };
		HANDLE write_handle{ nullptr };

		if (!::CreatePipe(&read_handle, &write_handle, &attributes, 0))
			return std::unexpected(MakeError(ProcessErrorCode::PipeCreationFailed, WindowsError(::GetLastError())));

		Pipe pipe{
			.Read = UniqueHandle{ read_handle },
			.Write = UniqueHandle{ write_handle },
		};

		/*
			The parent-side read handle must never enter the child.
			Otherwise EOF may never arrive after the child exits.
		*/
		if (!::SetHandleInformation(pipe.Read.Get(), HANDLE_FLAG_INHERIT, 0)) 
			return std::unexpected(MakeError(ProcessErrorCode::PipeCreationFailed, WindowsError(::GetLastError())));

		return pipe;
	}

	[[nodiscard]] std::expected<std::wstring, ProcessError>
		Utf8ToWide(std::string_view value) {
		if (value.empty())
			return std::wstring{};

		const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (required <= 0)
			return std::unexpected(MakeError(ProcessErrorCode::ArgumentEncodingFailed, WindowsError(::GetLastError())));

		std::wstring result(static_cast<std::size_t>(required), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required) <= 0) 
			return std::unexpected(MakeError(ProcessErrorCode::ArgumentEncodingFailed, WindowsError(::GetLastError())));

		return result;
	}

	[[nodiscard]] std::wstring QuoteArgument(std::wstring_view argument) {
		const bool requires_quotes = argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
		if (!requires_quotes)
			return std::wstring{ argument };

		std::wstring result;
		result.push_back(L'"');

		std::size_t backslashes{ 0 };
		for (const wchar_t character : argument) {
			if (character == L'\\') {
				++backslashes;
				continue;
			}

			if (character == L'"') {
				result.append((backslashes * 2) + 1, L'\\');
				result.push_back(L'"');
				backslashes = 0;
				continue;
			}

			result.append(backslashes, L'\\');

			backslashes = 0;
			result.push_back(character);
		}

		/*
			Backslashes immediately before the closing quote must
			be doubled or the child CRT may interpret them as
			escaping that quote.
		*/
		result.append(backslashes * 2, L'\\');

		result.push_back(L'"');
		return result;
	}

	[[nodiscard]] std::expected<std::wstring, ProcessError> BuildCommandLine(const ProcessRequest& request) {
		std::wstring command_line = QuoteArgument(request.Executable.wstring());

		for (const auto& argument : request.Arguments) {
			auto wide = Utf8ToWide(argument);
			if (!wide)
				return std::unexpected(wide.error());

			command_line.push_back(L' ');
			command_line += QuoteArgument(*wide);
		}

		return command_line;
	}

	void AddObservedBytes(PipeReadState& state, std::size_t bytes) noexcept {
		if (bytes > std::numeric_limits<std::size_t>::max() - state.ObservedBytes) {
			state.ObservedBytes = std::numeric_limits<std::size_t>::max();
			return;
		}

		state.ObservedBytes += bytes;
	}

	void ReadPipe(HANDLE handle, std::size_t limit, std::string& output, PipeReadState& state) noexcept {
		std::array<char, 16 * 1024> buffer{};

		for (;;) {
			DWORD bytes_read{ 0 };
			const BOOL success = ::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);
			if (!success) {
				const DWORD error = ::GetLastError();
				if (error == ERROR_BROKEN_PIPE)
					break;

				state.Error = WindowsError(error);
				break;
			}

			if (bytes_read == 0) break;

			const auto size = static_cast<std::size_t>(bytes_read);
			AddObservedBytes(state, size);
			const std::size_t remaining = output.size() >= limit ? 0 : limit - output.size();
			const std::size_t copy_size = std::min(remaining, size);
			output.append(buffer.data(), copy_size);
			if (copy_size < size)
				state.LimitExceeded = true;
		}
	}
}

namespace Aurore::Data::Detail {
	std::expected<ProcessResult, ProcessError> Process::Run(const ProcessRequest& request) {
		if (request.Executable.empty()) 
			return std::unexpected(MakeError(ProcessErrorCode::EmptyExecutable));

		auto command_line = BuildCommandLine(request);
		if (!command_line)
			return std::unexpected(command_line.error());

		auto stdout_pipe = CreatePipePair();
		if (!stdout_pipe)
			return std::unexpected(stdout_pipe.error());

		auto stderr_pipe = CreatePipePair();
		if (!stderr_pipe)
			return std::unexpected(stderr_pipe.error());

		SECURITY_ATTRIBUTES attributes{
			.nLength = sizeof(SECURITY_ATTRIBUTES),
			.lpSecurityDescriptor = nullptr,
			.bInheritHandle = TRUE,
		};

		UniqueHandle standard_input{ ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
		if (!standard_input)
			return std::unexpected(MakeError(ProcessErrorCode::StandardHandleCreationFailed, WindowsError(::GetLastError())));

		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = standard_input.Get();
		startup.StartupInfo.hStdOutput = stdout_pipe->Write.Get();
		startup.StartupInfo.hStdError = stderr_pipe->Write.Get();

		SIZE_T attribute_bytes{ 0 };
		::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
		if (attribute_bytes == 0) 
			return std::unexpected(MakeError(ProcessErrorCode::ProcessCreationFailed, WindowsError(::GetLastError())));

		const std::size_t storage_count = (attribute_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
		std::vector<std::max_align_t> attribute_storage(storage_count);
		startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
		if (!::InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
			return std::unexpected(MakeError(ProcessErrorCode::ProcessCreationFailed, WindowsError(::GetLastError())));

		HANDLE inherited_handles[]{
			standard_input.Get(),
			stdout_pipe->Write.Get(),
			stderr_pipe->Write.Get(),
		};

		if (!::UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles, sizeof(inherited_handles), nullptr, nullptr)) {
			const auto error = WindowsError(::GetLastError());
			::DeleteProcThreadAttributeList(startup.lpAttributeList);
			return std::unexpected(MakeError(ProcessErrorCode::ProcessCreationFailed, error));
		}

		std::vector<wchar_t> mutable_command_line(command_line->begin(), command_line->end());
		mutable_command_line.push_back(L'\0');
		PROCESS_INFORMATION process_info{};
		const BOOL created = ::CreateProcessW(request.Executable.c_str(), mutable_command_line.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &process_info);
		const DWORD creation_error = created ? ERROR_SUCCESS : ::GetLastError();
		::DeleteProcThreadAttributeList(startup.lpAttributeList);
		if (!created)
			return std::unexpected(MakeError(ProcessErrorCode::ProcessCreationFailed, WindowsError(creation_error)));

		UniqueHandle process{ process_info.hProcess };
		UniqueHandle thread{ process_info.hThread };

		/*
			These are child-side handles. Keeping parent copies open
			would prevent the readers from observing EOF.
		*/
		stdout_pipe->Write.Reset();
		stderr_pipe->Write.Reset();
		standard_input.Reset();

		ProcessResult result;
		PipeReadState stdout_state;
		PipeReadState stderr_state;

		std::thread stdout_reader;
		std::thread stderr_reader;

		try {
			stdout_reader = std::thread{ [&] { ReadPipe(stdout_pipe->Read.Get(), request.MaximumStandardOutputBytes, result.StdOut, stdout_state); } };
			stderr_reader = std::thread{ [&] { ReadPipe(stderr_pipe->Read.Get(), request.MaximumStandardErrorBytes, result.StdErr, stderr_state); } };
		}
		catch (const std::system_error& error) {
			(void)::TerminateProcess(process.Get(), 1);
			(void)::WaitForSingleObject(process.Get(), INFINITE);

			if (stdout_reader.joinable())
				stdout_reader.join();

			if (stderr_reader.joinable())
				stderr_reader.join();

			return std::unexpected(MakeError(ProcessErrorCode::ReaderCreationFailed, error.code()));
		}

		const DWORD wait_result = ::WaitForSingleObject(process.Get(), INFINITE);
		if (wait_result != WAIT_OBJECT_0) {
			const auto error = WindowsError(::GetLastError());
			(void)::TerminateProcess(process.Get(), 1);
			(void)::WaitForSingleObject(process.Get(), INFINITE);

			stdout_reader.join();
			stderr_reader.join();
			return std::unexpected(MakeError(ProcessErrorCode::WaitFailed, error));
		}

		stdout_reader.join();
		stderr_reader.join();
		if (stdout_state.Error)
			return std::unexpected(MakeError(ProcessErrorCode::OutputReadFailed, stdout_state.Error));

		if (stderr_state.Error)
			return std::unexpected(MakeError(ProcessErrorCode::OutputReadFailed, stderr_state.Error));

		if (stdout_state.LimitExceeded)
			return std::unexpected(MakeError(ProcessErrorCode::StandardOutputLimitExceeded, {}, stdout_state.ObservedBytes, request.MaximumStandardOutputBytes));

		if (stderr_state.LimitExceeded) 
			return std::unexpected(MakeError(ProcessErrorCode::StandardErrorLimitExceeded, {}, stderr_state.ObservedBytes, request.MaximumStandardErrorBytes));

		DWORD exit_code{ 0 };
		if (!::GetExitCodeProcess(process.Get(), &exit_code)) 
			return std::unexpected(MakeError(ProcessErrorCode::ExitCodeUnavailable, WindowsError(::GetLastError())));

		result.ExitCode = static_cast<int>(exit_code);

		return result;
	}
}
