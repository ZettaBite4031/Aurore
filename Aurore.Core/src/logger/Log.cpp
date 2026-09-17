#include <Aurore/Core/logger/Log.hpp>

#include <algorithm>
#include <iostream>
#include <format>

namespace Aurore::Core {
	namespace {
		constexpr const char* ANSIReset = "\x1b[0m";

		static const char* LevelANSIColors(LogLevel level) {
			switch (level) {
			case LogLevel::Trace: return "\x1b[2;37m";
			case LogLevel::Debug: return "\x1b[90m";
			case LogLevel::Info: return "\x1b[39m";
			case LogLevel::Warn: return "\x1b[33m";
			case LogLevel::Error: return "\x1b[91m";
			case LogLevel::Fatal: return "\x1b[30;41m";
			}
			return ANSIReset;
		}

		static const char* LevelToString(LogLevel level) {
			switch (level) {
			case LogLevel::Trace: return "TRACE";
			case LogLevel::Debug: return "?DEBUG?";
			case LogLevel::Info: return "-INFO-";
			case LogLevel::Warn: return "~WARN~";
			case LogLevel::Error: return "!ERROR!";
			case LogLevel::Fatal: return "+!FATAL!+";
			}
			return "UNKNOWN";
		}

		static const char* CategoryToString(LogCategory category) {
			switch (category) {
			case LogCategory::Core: return "CORE";
			case LogCategory::Networking: return "NETWORKING";
			case LogCategory::Protocol: return "PROTOCOL";
			case LogCategory::World: return "WORLD";
			case LogCategory::Chunk: return "CHUNK";
			case LogCategory::Entity: return "ENTITY";
			case LogCategory::Player: return "PLAYER";
			case LogCategory::Registry: return "REGISTRY";
			case LogCategory::Storage: return "STORAGE";
			case LogCategory::Command: return "COMMAND";
			case LogCategory::General: return "GENERAL";
			}
			return "UNKNOWN";
		}
	} // anonymous namespace

	void ConsoleSink::Write(const LogMessage& msg) {
		std::cout << LevelANSIColors(msg.Level);

		{
			using namespace std::chrono;
			std::cout
				<< "["
				<< std::format("{:%H:%M:%S}", floor<seconds>(msg.Time))
				<< "] ";
		}

		std::cout
			<< "("
			<< LevelToString(msg.Level)
			<< ") ";

		std::cout
			<< CategoryToString(msg.Category)
			<< " - "
			<< msg.Message
			<< ANSIReset
			<< '\n';
	}

	std::string ConsoleSink::Identifier() const {
		return "Console";
	}

	FileSink::FileSink(std::string filename)
		: m_Filename(std::move(filename)), m_File(m_Filename, std::ios::app) {}

	void FileSink::Write(const LogMessage& msg) {
		if (!m_File) return;

		{
			using namespace std::chrono;
			m_File
				<< "["
				<< std::format("{:%H:%M:%S}", floor<seconds>(msg.Time))
				<< "] ";
		}

		m_File
			<< "("
			<< LevelToString(msg.Level)
			<< ") ";

		m_File
			<< CategoryToString(msg.Category)
			<< " - "
			<< msg.Message;

		m_File
			<< " @ "
			<< msg.Location.file_name()
			<< ":"
			<< msg.Location.line()
			<< ":"
			<< msg.Location.function_name()
			<< '\n';
	}

	std::string FileSink::Identifier() const {
		return "File:" + m_Filename;
	}

	AsyncLogger::AsyncLogger() {
		m_Running = true;
		m_Thread = std::thread(&AsyncLogger::Run, this);
	}

	AsyncLogger::~AsyncLogger() {
		{
			std::lock_guard lock(m_Mutex);
			m_Running = false;
		}

		m_Condition.notify_one();

		if (m_Thread.joinable()) m_Thread.join();
	}

	void AsyncLogger::Submit(LogMessage msg) {
		{
			std::lock_guard lock(m_Mutex);
			m_Messages.push(std::move(msg));
		}
		m_Condition.notify_one();
	}

	void AsyncLogger::AddSink(std::shared_ptr<LogSink> new_sink) {
		for (auto& sink : m_Sinks)
			if (sink->Identifier() == new_sink->Identifier()) return;

		m_Sinks.push_back(std::move(new_sink));
	}

	void AsyncLogger::Stop() {
		{
			std::lock_guard lock(m_Mutex);
			m_Running = false;
		}

		m_Condition.notify_one();
		if (m_Thread.joinable()) m_Thread.join();
	}

	void AsyncLogger::Run() {
		while (true) {
			LogMessage msg;

			{
				std::unique_lock lock(m_Mutex);

				m_Condition.wait(lock, [&]() {
					return !m_Messages.empty() || !m_Running;
				});

				if (!m_Running && m_Messages.empty()) break;

				msg = std::move(m_Messages.front());
				m_Messages.pop();
			}

			for (auto& sink : m_Sinks) sink->Write(msg);
		}
	}

	Logger& Logger::Get() {
		static Logger instance;
		return instance;
	}

	Logger::Logger() {
		m_AsyncLogger.AddSink(std::make_shared<ConsoleSink>());
	}

	void Logger::Write(LogMessage msg) {
		m_AsyncLogger.Submit(std::move(msg));
	}

	void Logger::AddSink(std::shared_ptr<LogSink> sink) {
		m_AsyncLogger.AddSink(std::move(sink));
	}

	void Logger::Init() {
		if (m_Initialized) return;
		m_Initialized = true;
	}

	void Logger::Stop() {
		if (!m_Initialized) return;
		m_AsyncLogger.Stop();
		m_Initialized = false;
	}

	bool Logger::ShouldLog(LogLevel level, LogCategory category) {
		return IsLevelEnabled(level) && IsCategoryEnabled(category);
	}

	void Logger::ToggleLevel(LogLevel level) {
		m_EnabledLevels ^= static_cast<uint32_t>(level);
	}

	void Logger::ToggleCategory(LogCategory category) {
		m_EnabledCategories ^= static_cast<uint32_t>(category);
	}

	namespace Log {
		void Initialize() {
			Logger::Get().Init();
		}

		void Shutdown() {
			Logger::Get().Stop();
		}

		bool IsLevelEnabled(LogLevel level) {
			return Logger::Get().IsLevelEnabled(level);
		}

		bool IsCategoryEnabled(LogCategory category) {
			return Logger::Get().IsCategoryEnabled(category);
		}

		void ToggleLevel(LogLevel level) {
			Logger::Get().ToggleLevel(level);
		}

		void ToggleCategory(LogCategory category) {
			Logger::Get().ToggleCategory(category);
		}

		void Write(LogLevel level, LogCategory category, std::string_view msg, std::source_location loc) {
			if (!Logger::Get().ShouldLog(level, category)) return;

			LogMessage log;
			log.Level = level;
			log.Category = category;
			log.Message = msg;
			log.Time = std::chrono::system_clock::now();
			log.ThreadId = std::this_thread::get_id();
			log.Location = loc;
			Logger::Get().Write(std::move(log));
		}
	} // namespace Log
} // namespace Aurore::Core
