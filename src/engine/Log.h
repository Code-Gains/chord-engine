#pragma once

#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace Engine {

enum class LogLevel {
    Info,
    Warn,
    Error
};

inline std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm buf;
#ifdef _WIN32
    localtime_s(&buf, &in_time_t);   // thread-safe on Windows
#else
    buf = *std::localtime(&in_time_t); // Linux/macOS
#endif

    std::stringstream ss;
    ss << std::put_time(&buf, "%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}


inline std::string_view GetLogLevelPrefix(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "[INFO] ";
        case LogLevel::Warn: return "[WARN] ";
        case LogLevel::Error: return "[ERROR] ";
    }

    return "[INFO] ";
}

inline void LogMessage(LogLevel level, std::string_view message) {
    std::cout << GetTimestamp() << ' ' << GetLogLevelPrefix(level) << message << '\n';
}

inline void Log(LogLevel level, std::string_view message) {
    LogMessage(level, message);
}

template <typename T>
inline void Log(LogLevel level, const T& value) {
    LogMessage(level, std::format("{}", value));
}

template <typename... Args>
inline void Log(LogLevel level, std::format_string<Args...> format, Args&&... args) {
    LogMessage(level, std::format(format, std::forward<Args>(args)...));
}

#define ENGINE_LOG_INFO(...) Engine::Log(Engine::LogLevel::Info, __VA_ARGS__)
#define ENGINE_LOG_WARN(...) Engine::Log(Engine::LogLevel::Warn, __VA_ARGS__)
#define ENGINE_LOG_ERROR(...) Engine::Log(Engine::LogLevel::Error, __VA_ARGS__)

} // namespace Engine
