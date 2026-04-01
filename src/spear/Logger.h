/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SPEAR_LOGGER_H
#define SPEAR_LOGGER_H

#include <string>
#include <mutex>

enum class LOGLEVEL {
    HIGHLIGHT = -999,
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
};

namespace TerminalColor {
const std::string reset  = "\033[0m";
const std::string black   = "\033[0m";
const std::string gray   = "\033[90m";
const std::string green  = "\033[32m";
const std::string yellow = "\033[33m";
const std::string red    = "\033[31m";
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

#endif // SPEAR_LOGGER_H