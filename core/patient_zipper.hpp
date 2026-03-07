#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace htj2k::core {

struct ZipResult {
  std::string patient_key;
  std::filesystem::path zip_path;
  bool ok = false;
  std::string message;
};

[[nodiscard]] std::vector<ZipResult> zip_patients(const std::filesystem::path& root, const ZipOptions& options);

}  // namespace htj2k::core
