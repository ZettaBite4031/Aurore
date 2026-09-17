#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <format>
#include <memory>
#include <mutex>
#include <queue>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Aurore::Core {
	enum class LogLevel {
		Trace = 1 << 0,
		Debug = 1 << 1,
		Info = 1 << 2,
		Warn = 1 << 3,
		Error = 1 << 4,
		Fatal = 1 << 5,
	};

	enum class LogCategory {
		Core = 1 << 0,
		Networking = 1 << 1,
		Protocol = 1 << 2,
		World = 1 << 3,
		Chunk = 1 << 4,
		Entity = 1 << 5,
		Player = 1 << 6,
		Registry = 1 << 7,
		Storage = 1 << 8,
		Command = 1 << 9,
		General = 1 << 10,
	};

	struct LogMessage {
		LogLevel Level;
		LogCategory Category;
		std::string Message;
		std::chrono::system_clock::time_point Time;
		std::thread::id ThreadId;
		std::source_location Location;
	};

	class LogSink {
	public:
		virtual ~LogSink() = default;
		virtual void Write(const LogMessage&) = 0;
		virtual std::string Identifier() const = 0;
	};

	class ConsoleSink final : public LogSink {
	public:
		void Write(const LogMessage&) override;
		std::string Identifier() const override;
	};

	class FileSink final : public LogSink {
	public:
		explicit FileSink(std::string);

		void Write(const LogMessage&) override;
		std::string Identifier() const override;
	private:
		std::string m_Filename;
		std::ofstream m_File;
	};

	class AsyncLogger {
	public:
		AsyncLogger();
		~AsyncLogger();

		void Submit(LogMessage);
		void AddSink(std::shared_ptr<LogSink>);

		void Stop();

	private:
		void Run();

		bool m_Running{ false };
		std::thread m_Thread;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::queue<LogMessage> m_Messages;
		std::vector<std::shared_ptr<LogSink>> m_Sinks;
	};

	class Logger {
	public:
		static Logger& Get();

		void Write(LogMessage);
		void AddSink(std::shared_ptr<LogSink>);
		void Init();
		void Stop();

		std::uint32_t GetEnabledLevels() const { return m_EnabledLevels; }
		bool IsLevelEnabled(LogLevel level) const { return m_EnabledLevels & static_cast<std::uint32_t>(level); }
		std::uint32_t GetEnabledCategories() const { return m_EnabledCategories; }
		bool IsCategoryEnabled(LogCategory category) const { return m_EnabledCategories & static_cast<std::uint32_t>(category); }

		void ToggleLevel(LogLevel);
		void ToggleCategory(LogCategory);

		bool ShouldLog(LogLevel, LogCategory);

	private:
		Logger();

		std::uint32_t m_EnabledLevels{ 0x3F };
		std::uint32_t m_EnabledCategories{ 0x7FF };
		bool m_Initialized{ false };
		AsyncLogger m_AsyncLogger;
	};

	namespace Log {
		void Initialize();
		void Shutdown();

		bool IsLevelEnabled(LogLevel level);

		void Write(LogLevel, LogCategory, std::string_view, std::source_location = std::source_location::current());

		template<typename... Args>
		void vWrite(std::source_location location, LogLevel level, LogCategory category, std::format_string<Args...> format, Args&&... args) {
			Write(level, category, std::format(format, std::forward<Args>(args)...), location);
		}
	} // namespace Log
} // namespace Aurore::Core

#define AU_TRACE(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Trace, \
	category, \
	__VA_ARGS__);

#define AU_DEBUG(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Debug, \
	category, \
	__VA_ARGS__);

#define AU_INFO(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Info, \
	category, \
	__VA_ARGS__);

#define AU_WARN(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Warn, \
	category, \
	__VA_ARGS__);

#define AU_ERROR(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Error, \
	category, \
	__VA_ARGS__);

#define AU_FATAL(category, ...) \
	Aurore::Core::Log::vWrite( \
	std::source_location::current(), \
	Aurore::Core::LogLevel::Fatal, \
	category, \
	__VA_ARGS__);
