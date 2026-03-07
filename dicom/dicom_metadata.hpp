#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <dcmtk/dcmdata/dctk.h>

#include "dicom/photometric.hpp"

namespace htj2k::dicom {

struct PaletteLut {
  std::vector<std::uint16_t> red;
  std::vector<std::uint16_t> green;
  std::vector<std::uint16_t> blue;
};

struct ImageSpec {
  std::uint32_t rows = 0;
  std::uint32_t columns = 0;
  std::uint16_t samples_per_pixel = 0;
  std::uint16_t bits_allocated = 0;
  std::uint16_t bits_stored = 0;
  std::uint16_t high_bit = 0;
  std::uint16_t pixel_representation = 0;
  std::string photometric_interpretation;
  std::string target_photometric_interpretation;
  std::optional<std::uint16_t> planar_configuration;
  std::uint32_t number_of_frames = 1;
  bool historically_lossy = false;
  bool has_pixel_data = false;
  bool is_float = false;
  bool is_double = false;
  PhotometricPlan photometric_plan{};
  PaletteLut palette{};

  [[nodiscard]] std::size_t bytes_per_sample() const { return bits_allocated / 8U; }
  [[nodiscard]] std::size_t source_frame_bytes() const
  {
    if (photometric_interpretation == "YBR_FULL_422" && bits_allocated == 8) {
      return static_cast<std::size_t>(rows) * (static_cast<std::size_t>(columns) + 1U) / 2U * 4U;
    }
    return static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns) *
           static_cast<std::size_t>(samples_per_pixel) * bytes_per_sample();
  }
  [[nodiscard]] std::size_t target_frame_bytes() const
  {
    return static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns) *
           static_cast<std::size_t>(photometric_plan.target_samples_per_pixel == 0 ? samples_per_pixel
                                                                                   : photometric_plan.target_samples_per_pixel) *
           bytes_per_sample();
  }
};

struct MetadataUpdatePlan {
  bool regenerate_sop_instance_uid = false;
};

[[nodiscard]] ImageSpec extract_image_spec(DcmDataset& dataset, E_TransferSyntax source_syntax);
void apply_output_metadata(DcmDataset& dataset, const ImageSpec& spec, const MetadataUpdatePlan& plan);

}  // namespace htj2k::dicom
