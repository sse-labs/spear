/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_LOGGER_H_
#define SRC_SPEAR_LOGGER_H_

#include <string>
#include <mutex>

enum class LOGLEVEL {
    HIGHLIGHT = -999,
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
};

namespace TerminalColor {
constexpr char reset[]  = "\033[0m";
constexpr char black[]   = "\033[0m";
constexpr char gray[]   = "\033[90m";
constexpr char green[]  = "\033[32m";
constexpr char yellow[] = "\033[33m";
constexpr char red[]    = "\033[31m";
}

class Logger {
 public:
    // Access the singleton instance
    static Logger& getInstance();

    // Deleted copy/move to enforce singleton
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Public logging interface
    void log(const std::string& message, LOGLEVEL level = LOGLEVEL::INFO);

    void setLogLevel(LOGLEVEL level);

 private:
    Logger() = default;  // private constructor

    std::string logLevelToStr(LOGLEVEL level);
    std::string timeStampStr();
    std::string getLogColor(LOGLEVEL level);

    LOGLEVEL currentLogLevel = LOGLEVEL::ERROR;
    std::mutex logMutex;  // ensures thread-safe logging
};

#endif  // SRC_SPEAR_LOGGER_H_
