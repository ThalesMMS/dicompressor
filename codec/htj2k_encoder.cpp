#include "codec/htj2k_encoder.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <openjph/ojph_base.h>
#include <openjph/ojph_mem.h>

namespace htj2k::codec {

std::uint32_t Htj2kEncoder::clamp_num_decompositions(const dicom::ImageSpec& spec, const std::uint32_t requested)
{
  std::uint32_t max_levels = 0;
  std::uint32_t width = spec.columns;
  std::uint32_t height = spec.rows;
  while (width > 1 && height > 1) {
    ++max_levels;
    width = (width + 1U) / 2U;
    height = (height + 1U) / 2U;
  }
  return std::min(requested, max_levels);
}

std::int32_t Htj2kEncoder::read_sample(const std::uint8_t* src,
                                       const std::size_t bytes_per_sample,
                                       const bool signed_pixel,
                                       const std::uint16_t bits_stored)
{
  switch (bytes_per_sample) {
    case 1:
      return signed_pixel ? static_cast<std::int32_t>(static_cast<std::int8_t>(*src))
                          : static_cast<std::int32_t>(*src);
    case 2: {
      const auto value = static_cast<std::uint16_t>(src[0]) |
                         static_cast<std::uint16_t>(static_cast<std::uint16_t>(src[1]) << 8U);
      return signed_pixel ? static_cast<std::int32_t>(static_cast<std::int16_t>(value))
                          : static_cast<std::int32_t>(value);
    }
    case 4: {
      const auto value = static_cast<std::uint32_t>(src[0]) |
                         (static_cast<std::uint32_t>(src[1]) << 8U) |
                         (static_cast<std::uint32_t>(src[2]) << 16U) |
                         (static_cast<std::uint32_t>(src[3]) << 24U);
      if (signed_pixel) {
        return static_cast<std::int32_t>(value);
      }
      if (bits_stored >= 32U && value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("32-bit unsigned full-range pixels are not supported in v1");
      }
      return static_cast<std::int32_t>(value);
    }
    default:
      throw std::runtime_error("unsupported sample size for HTJ2K encode");
  }
}

EncodeResult Htj2kEncoder::encode(const dicom::ImageSpec& spec,
                                  const FrameView& frame,
                                  const EncodeOptions& options)
{
  if (frame.data == nullptr || frame.size_bytes == 0) {
    throw std::runtime_error("empty frame passed to Htj2kEncoder");
  }

  output_.close();
  output_.open(capacity_hint_);
  codestream_.restart();
  codestream_.set_planar(true);

  auto siz = codestream_.access_siz();
  siz.set_image_offset(ojph::point(0, 0));
  siz.set_tile_offset(ojph::point(0, 0));
  siz.set_tile_size(ojph::size(spec.columns, spec.rows));
  siz.set_image_extent(ojph::point(spec.columns, spec.rows));
  siz.set_num_components(spec.photometric_plan.target_samples_per_pixel);
  for (std::uint32_t component = 0; component < spec.photometric_plan.target_samples_per_pixel; ++component) {
    siz.set_component(
      component,
      ojph::point(1, 1),
      spec.bits_stored,
      spec.pixel_representation == 1);
  }

  auto cod = codestream_.access_cod();
  cod.set_num_decomposition(clamp_num_decompositions(spec, options.num_decomps));
  cod.set_block_dims(options.block_size.width, options.block_size.height);
  cod.set_reversible(true);
  cod.set_color_transform(false);

  codestream_.write_headers(&output_);

  const auto bytes_per_sample = spec.bytes_per_sample();
  const auto samples_per_pixel = spec.photometric_plan.target_samples_per_pixel;

  ojph::ui32 component_index = 0;
  ojph::line_buf* line = codestream_.exchange(nullptr, component_index);
  std::vector<std::uint32_t> row_index(samples_per_pixel, 0);
  while (line != nullptr) {
    const auto current_row = row_index[component_index];
    const auto* row_ptr = frame.data + static_cast<std::size_t>(current_row) * frame.row_stride_bytes;

    if (samples_per_pixel == 1) {
      for (std::uint32_t x = 0; x < spec.columns; ++x) {
        line->i32[x] = read_sample(row_ptr + static_cast<std::size_t>(x) * bytes_per_sample,
                                   bytes_per_sample,
                                   spec.pixel_representation == 1,
                                   spec.bits_stored);
      }
    } else {
      for (std::uint32_t x = 0; x < spec.columns; ++x) {
        const auto* pixel_ptr =
          row_ptr + static_cast<std::size_t>(x) * samples_per_pixel * bytes_per_sample +
          static_cast<std::size_t>(component_index) * bytes_per_sample;
        line->i32[x] = read_sample(pixel_ptr, bytes_per_sample, spec.pixel_representation == 1, spec.bits_stored);
      }
    }

    ++row_index[component_index];
    line = codestream_.exchange(line, component_index);
  }

  codestream_.flush();
  capacity_hint_ = std::max(capacity_hint_, output_.get_used_size());

  EncodeResult result;
  result.codestream.assign(output_.get_data(), output_.get_data() + output_.get_used_size());
  output_.close();
  return result;
}

}  // namespace htj2k::codec
