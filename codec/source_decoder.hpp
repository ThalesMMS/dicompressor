#pragma once

#include <filesystem>
#include <memory>

#include "codec/htj2k_encoder.hpp"
#include "dicom/dicom_metadata.hpp"
#include "dicom/dicom_reader.hpp"

namespace htj2k::codec {

class ISourceDecoder {
public:
  virtual ~ISourceDecoder() = default;

  [[nodiscard]] virtual const dicom::ImageSpec& image_spec() const = 0;
  [[nodiscard]] virtual std::size_t frame_count() const = 0;
  [[nodiscard]] virtual FrameView decode_frame(std::size_t index, OwnedFrameBuffer& scratch) = 0;
};

[[nodiscard]] std::unique_ptr<ISourceDecoder> create_source_decoder(const dicom::LoadedDicom& loaded,
                                                                    dicom::ImageSpec spec);

}  // namespace htj2k::codec
