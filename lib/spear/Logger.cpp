/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "Logger.h"

#include <chrono>
#include <iostream>
#include <string>

Logger& Logger::getInstance() {
    static Logger instance;  // Thread-safe since C++11
    return instance;
}

void Logger::setLogLevel(LOGLEVEL level) {
    this->currentLogLevel = level;
}

void Logger::log(const std::string &message, LOGLEVEL level) {
    auto logTag = logLevelToStr(level);
    auto timeStampTag = timeStampStr();
    auto colorResetTag = TerminalColor::reset;
    auto logColor = getLogColor(level);

    if (level <= currentLogLevel) {
        std::cout << logColor << timeStampTag << " " << logTag << " " << message << colorResetTag << std::endl;
    }
}

std::string Logger::timeStampStr() {
    auto currentTimePoint = std::chrono::system_clock::now();
    std::time_t currentTime = std::chrono::system_clock::to_time_t(currentTimePoint);

    std::tm timeInfo{};
    localtime_r(&currentTime, &timeInfo);

    std::ostringstream stringStream;
    stringStream << "[" << std::put_time(&timeInfo, "%H:%M:%S") << "]";

    return stringStream.str();
}

std::string Logger::logLevelToStr(LOGLEVEL level) {
    switch (level) {
        case LOGLEVEL::HIGHLIGHT:
            return  "[INFO] ";
        case LOGLEVEL::INFO:
            return  "[INFO] ";
        case LOGLEVEL::WARNING:
            return "[WARNING] ";
        case LOGLEVEL::ERROR:
            return "[ERROR] ";
        default:
            return "";
    }
}

std::string Logger::getLogColor(LOGLEVEL level) {
    switch (level) {
        case LOGLEVEL::INFO:
            return TerminalColor::black;
        case LOGLEVEL::WARNING:
            return TerminalColor::yellow;
        case LOGLEVEL::ERROR:
            return TerminalColor::red;
        case LOGLEVEL::HIGHLIGHT:
            return TerminalColor::green;
        default:
            return TerminalColor::reset;
    }
}
