#pragma once

#include <filesystem>

#include "core/types.hpp"

namespace htj2k::core {

[[nodiscard]] DiscoveryResult discover_files(const std::filesystem::path& input_root);

}  // namespace htj2k::core
