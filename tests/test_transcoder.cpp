#include "tests/test_macros.hpp"

#include <filesystem>
#include "core/transcoder.hpp"
#include "dicom/dicom_reader.hpp"

#include <dcmtk/dcmdata/dcuid.h>

namespace {

void create_test_dicom(const std::filesystem::path& path,
                       const std::uint16_t bits_allocated,
                       const std::vector<std::uint8_t>& pixel_bytes)
{
  DcmFileFormat file_format;
  auto* dataset = file_format.getDataset();
  char study_uid[100];
  char series_uid[100];
  char instance_uid[100];
  dcmGenerateUniqueIdentifier(study_uid, SITE_STUDY_UID_ROOT);
  dcmGenerateUniqueIdentifier(series_uid, SITE_SERIES_UID_ROOT);
  dcmGenerateUniqueIdentifier(instance_uid, SITE_INSTANCE_UID_ROOT);

  dataset->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
  dataset->putAndInsertString(DCM_SOPInstanceUID, instance_uid);
  dataset->putAndInsertString(DCM_StudyInstanceUID, study_uid);
  dataset->putAndInsertString(DCM_SeriesInstanceUID, series_uid);
  dataset->putAndInsertString(DCM_PatientName, "Test^Patient");
  dataset->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
  dataset->putAndInsertUint16(DCM_SamplesPerPixel, 1);
  dataset->putAndInsertUint16(DCM_Rows, 4);
  dataset->putAndInsertUint16(DCM_Columns, 4);
  dataset->putAndInsertUint16(DCM_BitsAllocated, bits_allocated);
  dataset->putAndInsertUint16(DCM_BitsStored, bits_allocated == 1 ? 1 : 8);
  dataset->putAndInsertUint16(DCM_HighBit, bits_allocated == 1 ? 0 : 7);
  dataset->putAndInsertUint16(DCM_PixelRepresentation, 0);
  dataset->putAndInsertUint8Array(DCM_PixelData, pixel_bytes.data(), pixel_bytes.size());
  file_format.saveFile(path.string().c_str(), EXS_LittleEndianExplicit);
}

}  // namespace

HTJ2K_TEST(test_transcoder_constructs)
{
  htj2k::BuildInfo build_info{"0.1", "clang", "Debug", "arm64"};
  htj2k::TranscodeOptions options;
  options.input_root = std::filesystem::temp_directory_path();
  options.output_root = std::filesystem::temp_directory_path() / "out";
  options.workers = 1;

  htj2k::core::Transcoder transcoder(build_info, options);
  (void)transcoder;
}

HTJ2K_TEST(test_transcoder_output_root)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-transcoder-output";
  const auto input = root / "input";
  const auto output = root / "output";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(input / "Patient");
  create_test_dicom(input / "Patient" / "image.dcm", 8, std::vector<std::uint8_t>(16, 42));

  htj2k::BuildInfo build_info{"0.1", "clang", "Debug", "arm64"};
  htj2k::TranscodeOptions options;
  options.input_root = input;
  options.output_root = output;
  options.workers = 1;

  htj2k::core::Transcoder transcoder(build_info, options);
  const auto report = transcoder.run();
  HTJ2K_ASSERT_EQ(report.summary().ok, 1U);
  HTJ2K_ASSERT(std::filesystem::exists(output / "Patient" / "image.dcm"));

  const auto loaded = htj2k::dicom::load_dicom_file(output / "Patient" / "image.dcm");
  HTJ2K_ASSERT_EQ(loaded.source_transfer_syntax, EXS_HighThroughputJPEG2000LosslessOnly);
  std::filesystem::remove_all(root);
}

HTJ2K_TEST(test_transcoder_in_place)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-transcoder-in-place";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Patient");
  const auto file_path = root / "Patient" / "image.dcm";
  create_test_dicom(file_path, 8, std::vector<std::uint8_t>(16, 7));

  htj2k::BuildInfo build_info{"0.1", "clang", "Debug", "arm64"};
  htj2k::TranscodeOptions options;
  options.input_root = root;
  options.in_place = true;
  options.workers = 1;

  htj2k::core::Transcoder transcoder(build_info, options);
  const auto report = transcoder.run();
  HTJ2K_ASSERT_EQ(report.summary().ok, 1U);

  const auto loaded = htj2k::dicom::load_dicom_file(file_path);
  HTJ2K_ASSERT_EQ(loaded.source_transfer_syntax, EXS_HighThroughputJPEG2000LosslessOnly);
  std::filesystem::remove_all(root);
}

HTJ2K_TEST(test_transcoder_copies_unsupported)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-transcoder-copy";
  const auto input = root / "input";
  const auto output = root / "output";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(input / "Patient");
  create_test_dicom(input / "Patient" / "image.dcm", 1, std::vector<std::uint8_t>(2, 0xFF));

  htj2k::BuildInfo build_info{"0.1", "clang", "Debug", "arm64"};
  htj2k::TranscodeOptions options;
  options.input_root = input;
  options.output_root = output;
  options.workers = 1;

  htj2k::core::Transcoder transcoder(build_info, options);
  const auto report = transcoder.run();
  HTJ2K_ASSERT_EQ(report.summary().copied, 1U);
  HTJ2K_ASSERT(std::filesystem::exists(output / "Patient" / "image.dcm"));
  std::filesystem::remove_all(root);
}
