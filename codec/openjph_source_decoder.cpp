#include "codec/openjph_source_decoder.hpp"

#include <stdexcept>

#include <openjph/ojph_base.h>
#include <openjph/ojph_codestream.h>
#include <openjph/ojph_file.h>
#include <openjph/ojph_mem.h>

namespace htj2k::codec {
namespace {

void pack_sample(std::vector<std::uint8_t>& output,
                 const std::size_t byte_offset,
                 const std::int32_t value,
                 const std::size_t bytes_per_sample)
{
  switch (bytes_per_sample) {
    case 1:
      output[byte_offset] = static_cast<std::uint8_t>(value);
      break;
    case 2: {
      const auto u = static_cast<std::uint16_t>(value);
      output[byte_offset] = static_cast<std::uint8_t>(u & 0xFFU);
      output[byte_offset + 1] = static_cast<std::uint8_t>((u >> 8U) & 0xFFU);
      break;
    }
    case 4: {
      const auto u = static_cast<std::uint32_t>(value);
      output[byte_offset] = static_cast<std::uint8_t>(u & 0xFFU);
      output[byte_offset + 1] = static_cast<std::uint8_t>((u >> 8U) & 0xFFU);
      output[byte_offset + 2] = static_cast<std::uint8_t>((u >> 16U) & 0xFFU);
      output[byte_offset + 3] = static_cast<std::uint8_t>((u >> 24U) & 0xFFU);
      break;
    }
    default:
      throw std::runtime_error("unsupported bytes per sample");
  }
}

}  // namespace

void decode_openjph_frame(const std::vector<std::uint8_t>& codestream,
                          const dicom::ImageSpec& spec,
                          OwnedFrameBuffer& output)
{
  if (spec.bits_allocated == 32 && spec.pixel_representation == 0 && spec.bits_stored >= 32U) {
    throw std::runtime_error("32-bit unsigned full-range HTJ2K decode is not supported in v1");
  }

  ojph::mem_infile input;
  input.open(codestream.data(), codestream.size());
  ojph::codestream decoder;
  decoder.read_headers(&input);
  decoder.create();

  output.resize(spec.target_frame_bytes());
  std::vector<std::uint32_t> row_index(spec.photometric_plan.target_samples_per_pixel, 0);
  const auto bytes_per_sample = spec.bytes_per_sample();
  const auto total_lines =
    static_cast<std::size_t>(spec.rows) * static_cast<std::size_t>(spec.photometric_plan.target_samples_per_pixel);

  ojph::ui32 component = 0;
  for (std::size_t line_index = 0; line_index < total_lines; ++line_index) {
    ojph::line_buf* line = decoder.pull(component);
    if (line == nullptr) {
      throw std::runtime_error("OpenJPH decode ended before the expected number of lines");
    }
    const auto row = row_index[component];
    for (std::uint32_t x = 0; x < spec.columns; ++x) {
      const auto pixel_index =
        (static_cast<std::size_t>(row) * spec.columns + x) * spec.photometric_plan.target_samples_per_pixel + component;
      pack_sample(output.bytes, pixel_index * bytes_per_sample, line->i32[x], bytes_per_sample);
    }
    ++row_index[component];
  }

  decoder.close();
  input.close();
}

}  // namespace htj2k::codec
