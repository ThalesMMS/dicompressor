#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <dcmtk/dcmdata/dcpixseq.h>

namespace htj2k::dicom {

inline constexpr Uint64 kFragmentItemHeaderSize = 8U;

struct StreamingPixelSequenceResult {
  std::unique_ptr<DcmPixelSequence> pixel_sequence;
  std::vector<Uint64> extended_offsets;
  std::vector<Uint64> extended_lengths;
};

class StreamingPixelSequenceBuilder {
public:
  StreamingPixelSequenceBuilder();
  StreamingPixelSequenceBuilder(const StreamingPixelSequenceBuilder&) = delete;
  StreamingPixelSequenceBuilder& operator=(const StreamingPixelSequenceBuilder&) = delete;

  void append_frame(std::vector<std::uint8_t>&& codestream);
  void append_frame(const std::vector<std::uint8_t>& codestream);
  void append_frame(std::span<const std::uint8_t> codestream);
  [[nodiscard]] StreamingPixelSequenceResult finalize();

private:
  [[nodiscard]] static Uint64 even_length(std::size_t length);

  std::unique_ptr<DcmPixelSequence> sequence_;
  DcmOffsetList offset_list_;
  Uint64 current_offset_ = 0;
  std::vector<Uint64> extended_offsets_;
  std::vector<Uint64> extended_lengths_;
  bool finalized_ = false;
};

}  // namespace htj2k::dicom
