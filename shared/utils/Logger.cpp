#include "utils/Logger.h"
#include <iostream>

void Logger::log(Level level, const std::string& msg) {
    const char* prefix = "";
    switch (level) {
        case Level::Debug: prefix = "[DEBUG] "; break;
        case Level::Info:  prefix = "[INFO] "; break;
        case Level::Warn:  prefix = "[WARN] "; break;
        case Level::Error: prefix = "[ERROR] "; break;
    }
    std::cerr << prefix << msg << std::endl;
}

void Logger::debug(const std::string& msg) { log(Level::Debug, msg); }
void Logger::info(const std::string& msg) { log(Level::Info, msg); }
void Logger::warn(const std::string& msg) { log(Level::Warn, msg); }
void Logger::error(const std::string& msg) { log(Level::Error, msg); }
