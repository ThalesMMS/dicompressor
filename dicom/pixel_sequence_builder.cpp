#include "dicom/pixel_sequence_builder.hpp"

#include <stdexcept>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcpxitem.h>

namespace htj2k::dicom {

std::unique_ptr<DcmPixelSequence> build_pixel_sequence(const std::vector<std::vector<std::uint8_t>>& codestreams,
                                                       std::vector<Uint64>& extended_offsets,
                                                       std::vector<Uint64>& extended_lengths)
{
  auto sequence = std::make_unique<DcmPixelSequence>(DCM_PixelSequenceTag);
  auto* offset_table = new DcmPixelItem(DCM_PixelItemTag);
  if (offset_table == nullptr) {
    throw std::bad_alloc{};
  }
  sequence->insert(offset_table);

  extended_offsets.clear();
  extended_lengths.clear();
  extended_offsets.reserve(codestreams.size());
  extended_lengths.reserve(codestreams.size());

  DcmOffsetList unused_offsets;
  Uint64 current_offset = 0;
  for (const auto& codestream : codestreams) {
    extended_offsets.push_back(current_offset);
    const Uint64 even_length = static_cast<Uint64>((codestream.size() + 1U) & ~std::size_t{1U});
    extended_lengths.push_back(even_length);

    auto* mutable_data = const_cast<Uint8*>(reinterpret_cast<const Uint8*>(codestream.data()));
    const auto status = sequence->storeCompressedFrame(
      unused_offsets,
      mutable_data,
      static_cast<Uint32>(codestream.size()),
      0);
    if (status.bad()) {
      throw std::runtime_error("failed to append compressed frame: " + std::string(status.text()));
    }

    current_offset += even_length + 8U;
  }

  return sequence;
}

}  // namespace htj2k::dicom
