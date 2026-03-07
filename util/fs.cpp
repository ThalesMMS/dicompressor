#include "util/fs.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "platform/fsync.hpp"

namespace htj2k::util {
namespace {

std::string random_suffix()
{
  thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<unsigned long long> dist;
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << dist(rng);
  return out.str();
}

}  // namespace

bool is_dcm_extension(const std::filesystem::path& path)
{
  const auto extension = lowercase_ascii(path.extension().string());
  return extension == ".dcm";
}

bool has_dicom_preamble(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }

  std::array<char, 132> buffer{};
  input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  if (input.gcount() < 132) {
    return false;
  }
  return buffer[128] == 'D' && buffer[129] == 'I' && buffer[130] == 'C' && buffer[131] == 'M';
}

std::filesystem::path unique_temp_path(const std::filesystem::path& target_path)
{
  const auto directory = target_path.parent_path();
  const auto filename = target_path.filename().string();
  return directory / ("." + filename + ".tmp." + random_suffix());
}

void ensure_parent_directory(const std::filesystem::path& path)
{
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
}

void atomic_write_file(const std::filesystem::path& target_path,
                       const std::function<void(const std::filesystem::path&)>& writer)
{
  ensure_parent_directory(target_path);
  const auto temp_path = unique_temp_path(target_path);

  try {
    writer(temp_path);
    platform::fsync_file(temp_path);
    std::filesystem::rename(temp_path, target_path);
    if (!target_path.parent_path().empty()) {
      platform::fsync_directory(target_path.parent_path());
    }
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    throw;
  }
}

void atomic_copy_file(const std::filesystem::path& source_path,
                      const std::filesystem::path& target_path,
                      const bool overwrite)
{
  ensure_parent_directory(target_path);
  if (std::filesystem::exists(target_path) && !overwrite) {
    throw std::runtime_error("destination exists and overwrite is disabled: " + target_path.string());
  }

  const auto temp_path = unique_temp_path(target_path);
  try {
    std::filesystem::copy_file(
      source_path,
      temp_path,
      std::filesystem::copy_options::overwrite_existing);
    platform::fsync_file(temp_path);
    if (overwrite && std::filesystem::exists(target_path)) {
      std::filesystem::remove(target_path);
    }
    std::filesystem::rename(temp_path, target_path);
    if (!target_path.parent_path().empty()) {
      platform::fsync_directory(target_path.parent_path());
    }
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    throw;
  }
}

std::string json_escape(std::string_view value)
{
  std::ostringstream out;
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20U) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec;
        } else {
          out << ch;
        }
        break;
    }
  }
  return out.str();
}

std::string lowercase_ascii(std::string value)
{
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

}  // namespace htj2k::util
