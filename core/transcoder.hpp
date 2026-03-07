#pragma once

#include "core/report.hpp"

namespace htj2k::core {

class Transcoder {
public:
  Transcoder(BuildInfo build_info, TranscodeOptions options);

  [[nodiscard]] TranscodeReport run();

private:
  [[nodiscard]] JobResult process_one(const std::filesystem::path& relative_path) const;
  [[nodiscard]] std::filesystem::path source_path_for(const std::filesystem::path& relative_path) const;
  [[nodiscard]] std::filesystem::path destination_path_for(const std::filesystem::path& relative_path) const;
  [[nodiscard]] std::string patient_key_for(const std::filesystem::path& relative_path) const;

  BuildInfo build_info_;
  TranscodeOptions options_;
};

}  // namespace htj2k::core
