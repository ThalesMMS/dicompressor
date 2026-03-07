#include "dicom/photometric.hpp"

#include <algorithm>
#include <stdexcept>

namespace htj2k::dicom {

bool is_supported_photometric(const std::string& photometric)
{
  return photometric == "MONOCHROME1" || photometric == "MONOCHROME2" || photometric == "PALETTE COLOR" ||
         photometric == "RGB" || photometric == "YBR_FULL" || photometric == "YBR_FULL_422" ||
         photometric == "YBR_RCT" || photometric == "YBR_ICT";
}

PhotometricPlan plan_photometric(const std::string& photometric,
                                 const std::uint16_t samples_per_pixel,
                                 const bool strict_color)
{
  if (photometric == "MONOCHROME1" || photometric == "MONOCHROME2") {
    return PhotometricPlan{photometric, 1, std::nullopt, false, false, false};
  }
  if (photometric == "RGB") {
    return PhotometricPlan{"RGB", 3, 0, false, false, false};
  }
  if (photometric == "YBR_FULL") {
    return PhotometricPlan{"YBR_FULL", 3, 0, false, false, false};
  }
  if (photometric == "YBR_FULL_422") {
    return PhotometricPlan{"YBR_FULL", 3, 0, false, true, false};
  }
  if (photometric == "PALETTE COLOR") {
    return PhotometricPlan{"RGB", 3, 0, true, false, false};
  }
  if (photometric == "YBR_RCT" || photometric == "YBR_ICT") {
    return PhotometricPlan{"RGB", 3, 0, false, false, true};
  }
  if (strict_color) {
    throw std::runtime_error("unsupported photometric interpretation: " + photometric);
  }
  return PhotometricPlan{photometric, samples_per_pixel, std::nullopt, false, false, false};
}

void expand_ybr_full_422(const std::vector<std::uint8_t>& packed,
                         const std::uint16_t columns,
                         const std::uint16_t rows,
                         std::vector<std::uint8_t>& expanded)
{
  const std::size_t pairs_per_row = static_cast<std::size_t>(columns + 1U) / 2U;
  const std::size_t expected = pairs_per_row * static_cast<std::size_t>(rows) * 4U;
  if (packed.size() < expected) {
    throw std::runtime_error("YBR_FULL_422 buffer shorter than expected");
  }

  expanded.resize(static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns) * 3U);
  std::size_t src = 0;
  std::size_t dst = 0;
  for (std::uint16_t row = 0; row < rows; ++row) {
    for (std::uint16_t col = 0; col < columns; col += 2) {
      const auto y1 = packed[src++];
      const auto y2 = packed[src++];
      const auto cb = packed[src++];
      const auto cr = packed[src++];

      expanded[dst++] = y1;
      expanded[dst++] = cb;
      expanded[dst++] = cr;

      if (col + 1 < columns) {
        expanded[dst++] = y2;
        expanded[dst++] = cb;
        expanded[dst++] = cr;
      }
    }
  }
}

void ybr_to_rgb_interleaved(std::vector<std::uint8_t>& buffer)
{
  for (std::size_t i = 0; i + 2 < buffer.size(); i += 3) {
    const int y = buffer[i];
    const int cb = buffer[i + 1] - 128;
    const int cr = buffer[i + 2] - 128;
    const auto clamp = [](const int value) -> std::uint8_t {
      return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    };

    const int r = static_cast<int>(y + 1.402 * cr);
    const int g = static_cast<int>(y - 0.344136 * cb - 0.714136 * cr);
    const int b = static_cast<int>(y + 1.772 * cb);
    buffer[i] = clamp(r);
    buffer[i + 1] = clamp(g);
    buffer[i + 2] = clamp(b);
  }
}

void apply_palette_color(const std::vector<std::uint16_t>& red_lut,
                         const std::vector<std::uint16_t>& green_lut,
                         const std::vector<std::uint16_t>& blue_lut,
                         const std::uint16_t bits_allocated,
                         const std::vector<std::uint8_t>& input,
                         std::vector<std::uint8_t>& output)
{
  if (bits_allocated != 8 && bits_allocated != 16) {
    throw std::runtime_error("PALETTE COLOR only supports 8 or 16 bits in v1");
  }
  const std::size_t pixels = bits_allocated == 8 ? input.size() : input.size() / 2U;
  const std::size_t bytes_per_sample = bits_allocated / 8U;
  output.resize(pixels * 3U * bytes_per_sample);

  const auto lookup = [&](const std::vector<std::uint16_t>& lut, const std::size_t index) -> std::uint16_t {
    const std::size_t bounded = std::min(index, lut.empty() ? 0U : lut.size() - 1U);
    return lut.empty() ? 0U : lut[bounded];
  };

  for (std::size_t i = 0; i < pixels; ++i) {
    std::size_t index = 0;
    if (bits_allocated == 8) {
      index = input[i];
    } else {
      index = static_cast<std::size_t>(input[2 * i]) | (static_cast<std::size_t>(input[2 * i + 1]) << 8U);
    }
    const auto r = lookup(red_lut, index);
    const auto g = lookup(green_lut, index);
    const auto b = lookup(blue_lut, index);
    if (bytes_per_sample == 1) {
      output[3 * i] = static_cast<std::uint8_t>(r >> 8U);
      output[3 * i + 1] = static_cast<std::uint8_t>(g >> 8U);
      output[3 * i + 2] = static_cast<std::uint8_t>(b >> 8U);
    } else {
      const auto base = i * 6U;
      output[base] = static_cast<std::uint8_t>(r & 0xFFU);
      output[base + 1] = static_cast<std::uint8_t>((r >> 8U) & 0xFFU);
      output[base + 2] = static_cast<std::uint8_t>(g & 0xFFU);
      output[base + 3] = static_cast<std::uint8_t>((g >> 8U) & 0xFFU);
      output[base + 4] = static_cast<std::uint8_t>(b & 0xFFU);
      output[base + 5] = static_cast<std::uint8_t>((b >> 8U) & 0xFFU);
    }
  }
}

}  // namespace htj2k::dicom
