#include "tests/test_macros.hpp"

#include <filesystem>
#include <fstream>

#include "core/report.hpp"

HTJ2K_TEST(test_report_json_generation)
{
  htj2k::TranscodeOptions options;
  options.input_root = "/tmp/in";
  options.output_root = "/tmp/out";
  htj2k::core::TranscodeReport report(options);
  report.set_build_info(htj2k::BuildInfo{"1.0", "clang", "Release", "arm64"});
  report.set_discovery(htj2k::DiscoveryResult{});
  report.add_result(htj2k::JobResult{});
  report.finalize(1.0);

  const auto path = std::filesystem::temp_directory_path() / "htj2k-report-test.json";
  report.write_json(path);
  HTJ2K_ASSERT(std::filesystem::exists(path));
  std::filesystem::remove(path);
}
