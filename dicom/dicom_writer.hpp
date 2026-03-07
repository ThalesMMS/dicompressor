#pragma once

#include <filesystem>

#include <dcmtk/dcmdata/dctk.h>

namespace htj2k::dicom {

void write_dicom_file(DcmFileFormat& file_format, const std::filesystem::path& target_path);

}  // namespace htj2k::dicom
