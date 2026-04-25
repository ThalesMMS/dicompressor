#include "dicom/pixel_sequence_builder.hpp"

#include <utility>

#include "dicom/streaming_pixel_sequence_builder.hpp"

namespace htj2k::dicom {

std::unique_ptr<DcmPixelSequence> build_pixel_sequence(const std::vector<std::vector<std::uint8_t>>& codestreams,
                                                       std::vector<Uint64>& extended_offsets,
                                                       std::vector<Uint64>& extended_lengths)
{
  StreamingPixelSequenceBuilder builder;
  for (const auto& codestream : codestreams) {
    builder.append_frame(codestream);
  }

  auto result = builder.finalize();
  extended_offsets = std::move(result.extended_offsets);
  extended_lengths = std::move(result.extended_lengths);
  return std::move(result.pixel_sequence);
}

}  // namespace htj2k::dicom
