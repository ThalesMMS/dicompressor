#pragma once

#include <filesystem>

namespace htj2k::platform {

void fsync_file(const std::filesystem::path& path);
void fsync_directory(const std::filesystem::path& path);

}  // namespace htj2k::platform
