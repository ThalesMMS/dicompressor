#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace htj2k::app {

struct CliParseResult {
  TranscodeOptions options;
  bool show_help = false;
};

[[nodiscard]] CliParseResult parse_cli(int argc, char** argv);
[[nodiscard]] std::string usage_text();
[[nodiscard]] BuildInfo current_build_info();

}  // namespace htj2k::app
