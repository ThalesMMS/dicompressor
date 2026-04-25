#include "tests/test_macros.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "codec/source_decoder.hpp"
#include "core/transcoder.hpp"
#include "dicom/dicom_metadata.hpp"
#include "dicom/dicom_reader.hpp"

#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcpixel.h>
#include <dcmtk/dcmdata/dcpixseq.h>
#include <dcmtk/dcmdata/dcpxitem.h>

namespace {

std::size_t source_pixel_bytes(const std::uint16_t bits_allocated,
                               const std::uint16_t rows,
                               const std::uint16_t columns,
                               const std::uint32_t number_of_frames)
{
  if (bits_allocated == 0 || (bits_allocated != 1 && bits_allocated % 8U != 0)) {
    throw std::invalid_argument("unsupported BitsAllocated for test DICOM fixture: " +
                                std::to_string(bits_allocated));
  }

  const auto pixels =
    static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns) * number_of_frames;
  return bits_allocated == 1 ? (pixels + 7U) / 8U : pixels * (bits_allocated / 8U);
}

std::vector<std::uint8_t> create_test_dicom(const std::filesystem::path& path,
                                            const std::uint16_t bits_allocated,
                                            const std::uint16_t rows = 4,
                                            const std::uint16_t columns = 4,
                                            const std::uint32_t number_of_frames = 1,
                                            const std::uint8_t seed = 0)
{
  std::vector<std::uint8_t> pixel_bytes(source_pixel_bytes(bits_allocated, rows, columns, number_of_frames));
  for (std::size_t i = 0; i < pixel_bytes.size(); ++i) {
    pixel_bytes[i] = static_cast<std::uint8_t>((seed + i) & 0xFFU);
  }

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
  dataset->putAndInsertUint16(DCM_Rows, rows);
  dataset->putAndInsertUint16(DCM_Columns, columns);
  dataset->putAndInsertUint16(DCM_BitsAllocated, bits_allocated);
  dataset->putAndInsertUint16(DCM_BitsStored, bits_allocated == 1 ? 1 : bits_allocated);
  dataset->putAndInsertUint16(DCM_HighBit, bits_allocated == 1 ? 0 : bits_allocated - 1U);
  dataset->putAndInsertUint16(DCM_PixelRepresentation, 0);
  if (number_of_frames > 1) {
    const auto frames = std::to_string(number_of_frames);
    dataset->putAndInsertString(DCM_NumberOfFrames, frames.c_str());
  }
  dataset->putAndInsertUint8Array(DCM_PixelData, pixel_bytes.data(), pixel_bytes.size());
  file_format.saveFile(path.string().c_str(), EXS_LittleEndianExplicit);
  return pixel_bytes;
}

Uint64 even_length(const Uint32 length)
{
  return static_cast<Uint64>((length + 1U) & ~Uint32{1U});
}

DcmPixelSequence& encapsulated_pixel_sequence(DcmDataset& dataset, const E_TransferSyntax syntax)
{
  DcmElement* element = nullptr;
  if (dataset.findAndGetElement(DCM_PixelData, element).bad() || element == nullptr) {
    throw std::runtime_error("PixelData element not found");
  }

  auto* pixel_data = dynamic_cast<DcmPixelData*>(element);
  if (pixel_data == nullptr) {
    throw std::runtime_error("PixelData element has unexpected type");
  }

  DcmPixelSequence* sequence = nullptr;
  if (pixel_data->getEncapsulatedRepresentation(syntax, nullptr, sequence).bad() || sequence == nullptr) {
    throw std::runtime_error("failed to get encapsulated pixel sequence");
  }
  return *sequence;
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
  create_test_dicom(input / "Patient" / "image.dcm", 8, 4, 4, 1, 42);

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

HTJ2K_TEST(test_transcoder_multiframe_extended_offset_table)
{
  constexpr std::uint16_t rows = 16;
  constexpr std::uint16_t columns = 16;
  constexpr std::uint32_t frame_count = 4;
  constexpr std::size_t frame_bytes = static_cast<std::size_t>(rows) * columns;

  const auto root = std::filesystem::temp_directory_path() / "htj2k-transcoder-multiframe";
  const auto input = root / "input";
  const auto output = root / "output";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(input / "Patient");

  const auto pixel_bytes = create_test_dicom(input / "Patient" / "cine.dcm", 8, rows, columns, frame_count, 17);

  htj2k::BuildInfo build_info{"0.1", "clang", "Debug", "arm64"};
  htj2k::TranscodeOptions options;
  options.input_root = input;
  options.output_root = output;
  options.workers = 1;

  htj2k::core::Transcoder transcoder(build_info, options);
  const auto report = transcoder.run();
  HTJ2K_ASSERT_EQ(report.summary().ok, 1U);

  const auto loaded = htj2k::dicom::load_dicom_file(output / "Patient" / "cine.dcm");
  HTJ2K_ASSERT_EQ(loaded.source_transfer_syntax, EXS_HighThroughputJPEG2000LosslessOnly);

  auto spec = htj2k::dicom::extract_image_spec(*loaded.dataset, loaded.source_transfer_syntax);
  HTJ2K_ASSERT_EQ(spec.number_of_frames, frame_count);

  const Uint64* offsets = nullptr;
  unsigned long offset_count = 0;
  HTJ2K_ASSERT(loaded.dataset->findAndGetUint64Array(DCM_ExtendedOffsetTable, offsets, &offset_count).good());
  HTJ2K_ASSERT(offsets != nullptr);
  HTJ2K_ASSERT_EQ(offset_count, static_cast<unsigned long>(frame_count));
  HTJ2K_ASSERT_EQ(offsets[0], Uint64{0});

  const Uint64* lengths = nullptr;
  unsigned long length_count = 0;
  HTJ2K_ASSERT(loaded.dataset->findAndGetUint64Array(DCM_ExtendedOffsetTableLengths, lengths, &length_count).good());
  HTJ2K_ASSERT(lengths != nullptr);
  HTJ2K_ASSERT_EQ(length_count, static_cast<unsigned long>(frame_count));

  auto& sequence = encapsulated_pixel_sequence(*loaded.dataset, loaded.source_transfer_syntax);
  HTJ2K_ASSERT_EQ(sequence.card(), static_cast<unsigned long>(frame_count + 1U));
  Uint64 expected_offset = 0;
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    DcmPixelItem* item = nullptr;
    HTJ2K_ASSERT(sequence.getItem(item, static_cast<unsigned long>(frame + 1U)).good());
    HTJ2K_ASSERT(item != nullptr);

    const auto padded_length = even_length(item->getLength());
    HTJ2K_ASSERT_EQ(offsets[frame], expected_offset);
    HTJ2K_ASSERT_EQ(lengths[frame], padded_length);
    expected_offset += padded_length + 8U;
    if (frame > 0) {
      HTJ2K_ASSERT(offsets[frame] > offsets[frame - 1U]);
    }
  }

  auto decoder = htj2k::codec::create_source_decoder(loaded, spec);
  htj2k::codec::OwnedFrameBuffer scratch;
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    const auto view = decoder->decode_frame(frame, scratch);
    HTJ2K_ASSERT_EQ(view.size_bytes, frame_bytes);
    const auto expected = pixel_bytes.data() + static_cast<std::size_t>(frame) * frame_bytes;
    HTJ2K_ASSERT(std::equal(expected, expected + frame_bytes, view.data));
  }

  std::filesystem::remove_all(root);
}

HTJ2K_TEST(test_transcoder_in_place)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-transcoder-in-place";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Patient");
  const auto file_path = root / "Patient" / "image.dcm";
  create_test_dicom(file_path, 8, 4, 4, 1, 7);

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
  create_test_dicom(input / "Patient" / "image.dcm", 1, 4, 4, 1, 0xFF);

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
