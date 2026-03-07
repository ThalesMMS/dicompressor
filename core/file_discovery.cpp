#include "core/file_discovery.hpp"

#include <dcmtk/dcmdata/dctk.h>

#include <system_error>

#include "util/fs.hpp"

namespace htj2k::core {
namespace {

bool is_valid_dicom_without_extension(const std::filesystem::path& path)
{
  if (util::has_dicom_preamble(path)) {
    return true;
  }

  DcmFileFormat file;
  return file.loadFile(path.string().c_str(), EXS_Unknown, EGL_noChange, 256, ERM_autoDetect).good();
}

bool should_ignore(const std::filesystem::path& path)
{
  const auto name = path.filename().string();
  return name == ".DS_Store" || name == "Thumbs.db";
}

}  // namespace

DiscoveryResult discover_files(const std::filesystem::path& input_root)
{
  DiscoveryResult result;
  result.directories.push_back(std::filesystem::path{});

  for (std::filesystem::recursive_directory_iterator it(input_root), end; it != end; ++it) {
    const auto& path = it->path();
    if (should_ignore(path)) {
      continue;
    }

    if (it->is_directory()) {
      result.directories.push_back(std::filesystem::relative(path, input_root));
      continue;
    }

    if (!it->is_regular_file()) {
      continue;
    }

    bool is_dicom = util::is_dcm_extension(path);
    if (!is_dicom) {
      is_dicom = is_valid_dicom_without_extension(path);
    }
    if (!is_dicom) {
      continue;
    }

    result.files.push_back(std::filesystem::relative(path, input_root));
    const auto file_size = std::filesystem::file_size(path);
    result.bytes_total += static_cast<std::uint64_t>(file_size);
  }

  return result;
}

}  // namespace htj2k::core
