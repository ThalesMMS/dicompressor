#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace htj2k::util {

[[nodiscard]] bool is_dcm_extension(const std::filesystem::path& path);
[[nodiscard]] bool has_dicom_preamble(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path unique_temp_path(const std::filesystem::path& target_path);
void ensure_parent_directory(const std::filesystem::path& path);
void atomic_write_file(const std::filesystem::path& target_path,
                       const std::function<void(const std::filesystem::path&)>& writer);
void atomic_copy_file(const std::filesystem::path& source_path,
                      const std::filesystem::path& target_path,
                      bool overwrite);
[[nodiscard]] std::string json_escape(std::string_view value);
[[nodiscard]] std::string lowercase_ascii(std::string value);

}  // namespace htj2k::util
