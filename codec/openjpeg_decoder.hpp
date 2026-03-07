#pragma once

#include <vector>

#include "codec/htj2k_encoder.hpp"
#include "dicom/dicom_metadata.hpp"

namespace htj2k::codec {

void decode_openjpeg_frame(const std::vector<std::uint8_t>& codestream,
                           const dicom::ImageSpec& spec,
                           OwnedFrameBuffer& output);

}  // namespace htj2k::codec
