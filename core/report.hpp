#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.hpp"

namespace htj2k::core {

class TranscodeReport {
public:
  explicit TranscodeReport(TranscodeOptions options);

  void set_build_info(BuildInfo build_info);
  void set_discovery(const DiscoveryResult& discovery);
  void finalize_discovery(double seconds);
  void add_result(JobResult result);
  void add_zip_result(const std::string& patient_key, bool ok, const std::string& message);
  void finalize(double total_seconds);
  void write_json(const std::filesystem::path& path) const;

  [[nodiscard]] int exit_code() const;
  [[nodiscard]] const SummaryCounters& summary() const { return summary_; }
  [[nodiscard]] const PhaseTimes& phase_totals() const { return phase_totals_; }
  [[nodiscard]] const std::vector<JobResult>& jobs() const { return jobs_; }
  [[nodiscard]] double total_seconds() const { return total_seconds_; }
  [[nodiscard]] double discovery_seconds() const { return discovery_seconds_; }
  [[nodiscard]] const BuildInfo& build_info() const { return build_info_; }
  [[nodiscard]] const TranscodeOptions& options() const { return options_; }
  [[nodiscard]] std::string summary_text() const;

private:
  TranscodeOptions options_;
  BuildInfo build_info_{};
  DiscoveryResult discovery_{};
  SummaryCounters summary_{};
  PhaseTimes phase_totals_{};
  std::vector<JobResult> jobs_;
  std::unordered_map<std::string, std::size_t> failure_reasons_;
  double discovery_seconds_ = 0.0;
  double total_seconds_ = 0.0;
};

}  // namespace htj2k::core
