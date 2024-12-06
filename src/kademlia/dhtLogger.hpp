#pragma once

#include <chrono>
#include <format>
#include <print>
#include <string_view>

class DHTLogger {
public:
    enum class LogLevel { DEBUG, INFO, WARNING, ERROR, CRITICAL };

    template<typename... Args>
    static void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto timeSinceEpoch = duration_cast<milliseconds>(now.time_since_epoch()).count();
        std::println("[{}] {} - {}", getLevelString(level), timeSinceEpoch,
                     std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static std::string_view getLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO";
            case LogLevel::WARNING:
                return "WARNING";
            case LogLevel::ERROR:
                return "ERROR";
            case LogLevel::CRITICAL:
                return "CRITICAL";
            default:
                return "UNKNOWN";
        }
    }
};
