#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace htj2k::dicom {

struct PhotometricPlan {
  std::string target_photometric;
  std::uint16_t target_samples_per_pixel = 0;
  std::optional<std::uint16_t> target_planar_configuration;
  bool needs_palette_expansion = false;
  bool needs_ybr422_expansion = false;
  bool needs_ybr_to_rgb_conversion = false;
};

[[nodiscard]] bool is_supported_photometric(const std::string& photometric);
[[nodiscard]] PhotometricPlan plan_photometric(const std::string& photometric,
                                              std::uint16_t samples_per_pixel,
                                              bool strict_color);
void expand_ybr_full_422(const std::vector<std::uint8_t>& packed,
                         std::uint16_t columns,
                         std::uint16_t rows,
                         std::vector<std::uint8_t>& expanded);
void ybr_to_rgb_interleaved(std::vector<std::uint8_t>& buffer);
void apply_palette_color(const std::vector<std::uint16_t>& red_lut,
                         const std::vector<std::uint16_t>& green_lut,
                         const std::vector<std::uint16_t>& blue_lut,
                         std::uint16_t bits_allocated,
                         const std::vector<std::uint8_t>& input,
                         std::vector<std::uint8_t>& output);

}  // namespace htj2k::dicom
