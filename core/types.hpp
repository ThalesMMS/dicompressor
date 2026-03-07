#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace htj2k {

enum class LogLevel : std::uint8_t {
  trace = 0,
  debug = 1,
  info = 2,
  warn = 3,
  error = 4,
};

enum class ZipMode : std::uint8_t {
  stored,
  deflated,
};

struct BlockSize {
  std::uint32_t width = 64;
  std::uint32_t height = 64;
};

struct EncodeOptions {
  std::uint32_t num_decomps = 5;
  BlockSize block_size{};
};

struct ZipOptions {
  bool enabled = false;
  ZipMode mode = ZipMode::stored;
};

struct BuildInfo {
  std::string version;
  std::string compiler;
  std::string build_type;
  std::string arch;
};

struct TranscodeOptions {
  std::filesystem::path input_root;
  std::filesystem::path output_root;
  bool in_place = false;
  bool overwrite = false;
  bool regenerate_sop_instance_uid = false;
  bool strict_color = false;
  bool benchmark_mode = false;
  std::optional<std::filesystem::path> report_json;
  std::size_t workers = 0;
  EncodeOptions encode{};
  ZipOptions zip{};
  LogLevel log_level = LogLevel::info;
};

struct PhaseTimes {
  double load_seconds = 0.0;
  double metadata_seconds = 0.0;
  double decode_seconds = 0.0;
  double encode_seconds = 0.0;
  double encapsulate_seconds = 0.0;
  double write_seconds = 0.0;
  double total_seconds = 0.0;

  PhaseTimes& operator+=(const PhaseTimes& other)
  {
    load_seconds += other.load_seconds;
    metadata_seconds += other.metadata_seconds;
    decode_seconds += other.decode_seconds;
    encode_seconds += other.encode_seconds;
    encapsulate_seconds += other.encapsulate_seconds;
    write_seconds += other.write_seconds;
    total_seconds += other.total_seconds;
    return *this;
  }
};

struct JobResult {
  std::filesystem::path source_path;
  std::filesystem::path destination_path;
  std::string patient_key;
  std::string status;
  std::string message;
  std::size_t frames = 0;
  std::uint64_t pixels = 0;
  std::uint64_t bytes_read = 0;
  std::uint64_t bytes_written = 0;
  PhaseTimes phase_times{};
};

struct DiscoveryResult {
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> directories;
  std::uint64_t bytes_total = 0;
};

struct SummaryCounters {
  std::size_t total = 0;
  std::size_t ok = 0;
  std::size_t copied = 0;
  std::size_t failed = 0;
  std::size_t zipped = 0;
  std::uint64_t frames = 0;
  std::uint64_t pixels = 0;
  std::uint64_t bytes_read = 0;
  std::uint64_t bytes_written = 0;
};

struct RuntimeStats {
  std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
  std::atomic<std::size_t> discovered{0};
  std::atomic<std::size_t> completed{0};
  std::atomic<std::size_t> ok{0};
  std::atomic<std::size_t> copied{0};
  std::atomic<std::size_t> failed{0};
  std::atomic<std::uint64_t> frames{0};
  std::atomic<std::uint64_t> pixels{0};
  std::atomic<std::uint64_t> bytes_read{0};
  std::atomic<std::uint64_t> bytes_written{0};
};

inline std::string_view to_string_view(const LogLevel level)
{
  switch (level) {
    case LogLevel::trace:
      return "trace";
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
  }
  return "info";
}

inline std::string_view to_string_view(const ZipMode mode)
{
  return mode == ZipMode::stored ? "stored" : "deflated";
}

}  // namespace htj2k
