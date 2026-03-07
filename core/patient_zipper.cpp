#include "core/patient_zipper.hpp"

#include <cstring>
#include <stdexcept>

#include "third_party/miniz/miniz.h"
#include "util/fs.hpp"

namespace htj2k::core {
namespace {

int compression_level(const ZipMode mode)
{
  return mode == ZipMode::stored ? MZ_NO_COMPRESSION : MZ_BEST_SPEED;
}

ZipResult zip_one_patient(const std::filesystem::path& root,
                          const std::filesystem::path& patient_dir,
                          const ZipOptions& options)
{
  ZipResult result;
  result.patient_key = patient_dir.filename().string();
  result.zip_path = root / (result.patient_key + ".zip");

  try {
    const auto temp_zip_path = util::unique_temp_path(result.zip_path);
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_file(&archive, temp_zip_path.string().c_str(), 0)) {
      throw std::runtime_error("miniz failed to initialize zip archive");
    }

    const int level = compression_level(options.mode);
    for (std::filesystem::recursive_directory_iterator it(patient_dir), end; it != end; ++it) {
      if (!it->is_regular_file()) {
        continue;
      }
      if (it->path() == result.zip_path || it->path() == temp_zip_path) {
        continue;
      }

      const auto archive_name = std::filesystem::relative(it->path(), root).generic_string();
      if (!mz_zip_writer_add_file(
            &archive,
            archive_name.c_str(),
            it->path().string().c_str(),
            nullptr,
            0,
            static_cast<mz_uint>(level))) {
        const auto error = mz_zip_get_error_string(mz_zip_get_last_error(&archive));
        mz_zip_writer_end(&archive);
        std::filesystem::remove(temp_zip_path);
        throw std::runtime_error(error != nullptr ? error : "miniz add_file failed");
      }
    }

    if (!mz_zip_writer_finalize_archive(&archive)) {
      const auto error = mz_zip_get_error_string(mz_zip_get_last_error(&archive));
      mz_zip_writer_end(&archive);
      std::filesystem::remove(temp_zip_path);
      throw std::runtime_error(error != nullptr ? error : "miniz finalize failed");
    }
    mz_zip_writer_end(&archive);

    if (std::filesystem::exists(result.zip_path)) {
      std::filesystem::remove(result.zip_path);
    }
    std::filesystem::rename(temp_zip_path, result.zip_path);

    result.ok = true;
    result.message = "zip created";
  } catch (const std::exception& ex) {
    result.ok = false;
    result.message = ex.what();
  }

  return result;
}

}  // namespace

std::vector<ZipResult> zip_patients(const std::filesystem::path& root, const ZipOptions& options)
{
  std::vector<ZipResult> results;
  if (!options.enabled) {
    return results;
  }

  for (std::filesystem::directory_iterator it(root), end; it != end; ++it) {
    if (!it->is_directory()) {
      continue;
    }
    results.push_back(zip_one_patient(root, it->path(), options));
  }
  return results;
}

}  // namespace htj2k::core
