#include "dicom/dicom_writer.hpp"

#include <stdexcept>

#include <dcmtk/dcmdata/dcuid.h>

#include "util/fs.hpp"

namespace htj2k::dicom {
namespace {

constexpr const char* kImplementationVersionName = "HTJ2KTC_0100";

}  // namespace

void write_dicom_file(DcmFileFormat& file_format, const std::filesystem::path& target_path)
{
  file_format.setImplementationClassUID(OFFIS_IMPLEMENTATION_CLASS_UID);
  file_format.setImplementationVersionName(kImplementationVersionName);

  util::atomic_write_file(target_path, [&](const std::filesystem::path& temp_path) {
    const auto status = file_format.saveFile(
      temp_path.string().c_str(),
      EXS_HighThroughputJPEG2000LosslessOnly,
      EET_UndefinedLength,
      EGL_recalcGL,
      EPD_withoutPadding,
      0,
      0,
      EWM_createNewMeta);
    if (status.bad()) {
      throw std::runtime_error("failed to save dicom: " + std::string(status.text()));
    }
  });
}

}  // namespace htj2k::dicom
