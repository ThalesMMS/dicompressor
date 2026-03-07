#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <openjph/ojph_codestream.h>
#include <openjph/ojph_file.h>
#include <openjph/ojph_params.h>

#include "core/types.hpp"
#include "dicom/dicom_metadata.hpp"

namespace htj2k::codec {

struct FrameView {
  const std::uint8_t* data = nullptr;
  std::size_t size_bytes = 0;
  std::size_t row_stride_bytes = 0;
};

struct OwnedFrameBuffer {
  std::vector<std::uint8_t> bytes;

  void resize(const std::size_t size_bytes) { bytes.resize(size_bytes); }
  [[nodiscard]] FrameView view(const std::size_t row_stride_bytes) const
  {
    return FrameView{bytes.data(), bytes.size(), row_stride_bytes};
  }
};

struct EncodeResult {
  std::vector<std::uint8_t> codestream;
};

class Htj2kEncoder {
public:
  Htj2kEncoder() = default;

  [[nodiscard]] EncodeResult encode(const dicom::ImageSpec& spec,
                                    const FrameView& frame,
                                    const EncodeOptions& options);

private:
  static std::uint32_t clamp_num_decompositions(const dicom::ImageSpec& spec, std::uint32_t requested);
  static std::int32_t read_sample(const std::uint8_t* src,
                                  std::size_t bytes_per_sample,
                                  bool signed_pixel,
                                  std::uint16_t bits_stored);

  ojph::codestream codestream_;
  ojph::mem_outfile output_;
  std::size_t capacity_hint_ = 1U << 16U;
};

}  // namespace htj2k::codec
