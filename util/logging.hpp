#pragma once

#include <string>
#include <string_view>

#include "core/types.hpp"

namespace htj2k::util {

void set_log_level(LogLevel level);
[[nodiscard]] LogLevel get_log_level();
void log(LogLevel level, std::string_view message);

void trace(std::string_view message);
void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}  // namespace htj2k::util
