#include "util/logging.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace htj2k::util {
namespace {

std::mutex g_log_mutex;
LogLevel g_log_level = LogLevel::info;

std::string timestamp_now()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

}  // namespace

void set_log_level(const LogLevel level) { g_log_level = level; }

LogLevel get_log_level() { return g_log_level; }

void log(const LogLevel level, std::string_view message)
{
  if (static_cast<int>(level) < static_cast<int>(g_log_level)) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::clog << "[" << timestamp_now() << "]"
            << "[" << to_string_view(level) << "]"
            << "[tid=" << std::this_thread::get_id() << "] "
            << message << '\n';
}

void trace(std::string_view message) { log(LogLevel::trace, message); }
void debug(std::string_view message) { log(LogLevel::debug, message); }
void info(std::string_view message) { log(LogLevel::info, message); }
void warn(std::string_view message) { log(LogLevel::warn, message); }
void error(std::string_view message) { log(LogLevel::error, message); }

}  // namespace htj2k::util
