#include "dicom/dicom_reader.hpp"

#include <filesystem>
#include <mutex>
#include <stdexcept>

#include <dcmtk/dcmdata/dcrledrg.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>

namespace htj2k::dicom {
namespace {

std::once_flag g_codec_registration_once;

}  // namespace

void register_dcmtk_codecs()
{
  std::call_once(g_codec_registration_once, [] {
    DJDecoderRegistration::registerCodecs(
      EDC_photometricInterpretation, EUC_default, EPC_default, OFFalse, OFFalse, OFFalse, OFFalse);
    DJLSDecoderRegistration::registerCodecs();
    DcmRLEDecoderRegistration::registerCodecs();
  });
}

LoadedDicom load_dicom_file(const std::filesystem::path& path)
{
  LoadedDicom loaded;
  loaded.file_size = static_cast<std::uint64_t>(std::filesystem::file_size(path));
  loaded.file_format = std::make_shared<DcmFileFormat>();
  const auto status = loaded.file_format->loadFile(path.string().c_str(), EXS_Unknown, EGL_noChange, DCM_MaxReadLength, ERM_autoDetect);
  if (status.bad()) {
    throw std::runtime_error("failed to load dicom: " + std::string(status.text()));
  }

  loaded.dataset = loaded.file_format->getDataset();
  loaded.source_transfer_syntax = loaded.dataset->getOriginalXfer();
  return loaded;
}

}  // namespace htj2k::dicom
