#pragma once

#include <string>

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static void log(Level level, const std::string& msg);
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
};
