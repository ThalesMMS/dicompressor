#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <dcmtk/dcmdata/dctk.h>

namespace htj2k::dicom {

std::unique_ptr<DcmPixelSequence> build_pixel_sequence(const std::vector<std::vector<std::uint8_t>>& codestreams,
                                                       std::vector<Uint64>& extended_offsets,
                                                       std::vector<Uint64>& extended_lengths);

}  // namespace htj2k::dicom
