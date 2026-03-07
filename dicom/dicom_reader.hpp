#pragma once

#include <filesystem>
#include <memory>

#include <dcmtk/dcmdata/dctk.h>

namespace htj2k::dicom {

struct LoadedDicom {
  std::shared_ptr<DcmFileFormat> file_format;
  DcmDataset* dataset = nullptr;
  E_TransferSyntax source_transfer_syntax = EXS_Unknown;
  std::uint64_t file_size = 0;
};

void register_dcmtk_codecs();
[[nodiscard]] LoadedDicom load_dicom_file(const std::filesystem::path& path);

}  // namespace htj2k::dicom
