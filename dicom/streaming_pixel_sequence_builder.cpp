#include "dicom/streaming_pixel_sequence_builder.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcpxitem.h>

namespace htj2k::dicom {

StreamingPixelSequenceBuilder::StreamingPixelSequenceBuilder()
    : sequence_(std::make_unique<DcmPixelSequence>(DCM_PixelSequenceTag))
{
  auto offset_table = std::make_unique<DcmPixelItem>(DCM_PixelItemTag);
  const auto status = sequence_->insert(offset_table.get());
  if (status.bad()) {
    throw std::runtime_error("failed to create pixel sequence offset table: " + std::string(status.text()));
  }
  offset_table.release();
}

void StreamingPixelSequenceBuilder::append_frame(std::vector<std::uint8_t>&& codestream)
{
  if (sequence_ == nullptr) {
    throw std::runtime_error("cannot append frame after pixel sequence finalization");
  }
  append_frame(std::span<const std::uint8_t>{codestream.data(), codestream.size()});
}

void StreamingPixelSequenceBuilder::append_frame(const std::vector<std::uint8_t>& codestream)
{
  append_frame(std::span<const std::uint8_t>{codestream.data(), codestream.size()});
}

void StreamingPixelSequenceBuilder::append_frame(const std::span<const std::uint8_t> codestream)
{
  if (sequence_ == nullptr) {
    throw std::runtime_error("cannot append frame after pixel sequence finalization");
  }
  if (codestream.empty()) {
    throw std::runtime_error("cannot append empty compressed frame");
  }
  if (codestream.size() > std::numeric_limits<Uint32>::max()) {
    throw std::runtime_error("compressed frame exceeds DCMTK item length limit");
  }

  auto* data = const_cast<Uint8*>(reinterpret_cast<const Uint8*>(codestream.data()));
  const auto status =
    sequence_->storeCompressedFrame(offset_list_, data, static_cast<Uint32>(codestream.size()), 0);
  if (status.bad()) {
    throw std::runtime_error("failed to append compressed frame: " + std::string(status.text()));
  }

  const auto padded_length = even_length(codestream.size());
  extended_offsets_.push_back(current_offset_);
  extended_lengths_.push_back(padded_length);
  current_offset_ += padded_length + kFragmentItemHeaderSize;
}

StreamingPixelSequenceResult StreamingPixelSequenceBuilder::finalize()
{
  if (finalized_) {
    throw std::logic_error("pixel sequence builder has already been finalized");
  }
  finalized_ = true;
  return StreamingPixelSequenceResult{
    std::move(sequence_),
    std::move(extended_offsets_),
    std::move(extended_lengths_),
  };
}

Uint64 StreamingPixelSequenceBuilder::even_length(const std::size_t length)
{
  return static_cast<Uint64>((length + 1U) & ~std::size_t{1U});
}

}  // namespace htj2k::dicom
