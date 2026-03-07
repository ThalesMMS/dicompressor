#include "app/cli.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace htj2k::app {
namespace {

std::size_t default_workers()
{
  const auto hint = std::thread::hardware_concurrency();
  return hint == 0 ? 1U : static_cast<std::size_t>(hint);
}

LogLevel parse_log_level(const std::string_view value)
{
  if (value == "trace") {
    return LogLevel::trace;
  }
  if (value == "debug") {
    return LogLevel::debug;
  }
  if (value == "info") {
    return LogLevel::info;
  }
  if (value == "warn") {
    return LogLevel::warn;
  }
  if (value == "error") {
    return LogLevel::error;
  }
  throw std::runtime_error("invalid log level: " + std::string(value));
}

BlockSize parse_block_size(const std::string_view value)
{
  const auto comma = value.find(',');
  if (comma == std::string_view::npos) {
    throw std::runtime_error("block size must be X,Y");
  }

  const auto width = static_cast<std::uint32_t>(std::stoul(std::string(value.substr(0, comma))));
  const auto height = static_cast<std::uint32_t>(std::stoul(std::string(value.substr(comma + 1))));
  if (width == 0 || height == 0) {
    throw std::runtime_error("block size must be positive");
  }
  return BlockSize{width, height};
}

ZipMode parse_zip_mode(const std::string_view value)
{
  if (value == "stored") {
    return ZipMode::stored;
  }
  if (value == "deflated") {
    return ZipMode::deflated;
  }
  throw std::runtime_error("invalid zip mode: " + std::string(value));
}

}  // namespace

CliParseResult parse_cli(const int argc, char** argv)
{
  CliParseResult result;
  auto& options = result.options;
  options.workers = default_workers();

  std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty()) {
    result.show_help = true;
    return result;
  }

  auto require_value = [&](const std::size_t index, const std::string_view name) -> std::string_view {
    if (index + 1 >= args.size()) {
      throw std::runtime_error("missing value for " + std::string(name));
    }
    return args[index + 1];
  };

  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto arg = args[i];
    if (arg == "-h" || arg == "--help") {
      result.show_help = true;
      return result;
    }
    if (arg == "--output-root") {
      options.output_root = require_value(i, arg);
      ++i;
      continue;
    }
    if (arg == "--in-place") {
      options.in_place = true;
      continue;
    }
    if (arg == "--zip-per-patient") {
      options.zip.enabled = true;
      continue;
    }
    if (arg == "--zip-mode") {
      options.zip.mode = parse_zip_mode(require_value(i, arg));
      ++i;
      continue;
    }
    if (arg == "--report-json") {
      options.report_json = std::filesystem::path(require_value(i, arg));
      ++i;
      continue;
    }
    if (arg == "--num-decomps") {
      options.encode.num_decomps =
        static_cast<std::uint32_t>(std::stoul(std::string(require_value(i, arg))));
      ++i;
      continue;
    }
    if (arg == "--block-size") {
      options.encode.block_size = parse_block_size(require_value(i, arg));
      ++i;
      continue;
    }
    if (arg == "--overwrite") {
      options.overwrite = true;
      continue;
    }
    if (arg == "--regenerate-sop-instance-uid") {
      options.regenerate_sop_instance_uid = true;
      continue;
    }
    if (arg == "--strict-color") {
      options.strict_color = true;
      continue;
    }
    if (arg == "--workers") {
      options.workers = static_cast<std::size_t>(std::stoul(std::string(require_value(i, arg))));
      ++i;
      continue;
    }
    if (arg == "--log-level") {
      options.log_level = parse_log_level(require_value(i, arg));
      ++i;
      continue;
    }
    if (arg == "--benchmark-mode") {
      options.benchmark_mode = true;
      continue;
    }
    if (!arg.starts_with("--") && options.input_root.empty()) {
      options.input_root = std::filesystem::path(arg);
      continue;
    }
    throw std::runtime_error("unknown argument: " + std::string(arg));
  }

  if (options.input_root.empty()) {
    throw std::runtime_error("input root is required");
  }
  if (options.in_place && !options.output_root.empty()) {
    throw std::runtime_error("use either --output-root or --in-place");
  }
  if (!options.in_place && options.output_root.empty()) {
    options.output_root = options.input_root;
    options.output_root += "-output";
  }
  if (options.workers == 0) {
    options.workers = 1;
  }
  return result;
}

std::string usage_text()
{
  std::ostringstream out;
  out << "Usage:\n"
      << "  transcode_htj2k <input_root> [--output-root PATH | --in-place]\n"
      << "                           [--zip-per-patient]\n"
      << "                           [--zip-mode stored|deflated]\n"
      << "                           [--report-json PATH]\n"
      << "                           [--num-decomps N]\n"
      << "                           [--block-size X,Y]\n"
      << "                           [--overwrite]\n"
      << "                           [--regenerate-sop-instance-uid]\n"
      << "                           [--strict-color]\n"
      << "                           [--workers N]\n"
      << "                           [--log-level trace|debug|info|warn|error]\n";
  return out.str();
}

BuildInfo current_build_info()
{
  BuildInfo info;
  info.version = HTJ2K_TRANSCODER_VERSION;
#if defined(__clang__)
  info.compiler = std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
  info.compiler = std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
  info.compiler = "MSVC";
#else
  info.compiler = "Unknown";
#endif

#if defined(NDEBUG)
  info.build_type = "Release";
#else
  info.build_type = "Debug";
#endif

#if defined(__aarch64__) || defined(__arm64__)
  info.arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  info.arch = "x86_64";
#else
  info.arch = "unknown";
#endif
  return info;
}

}  // namespace htj2k::app
