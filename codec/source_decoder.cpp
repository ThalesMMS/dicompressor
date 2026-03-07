#include "codec/source_decoder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfcache.h>
#include <dcmtk/dcmdata/dcpxitem.h>

#include "codec/openjpeg_decoder.hpp"
#include "codec/openjph_source_decoder.hpp"
#include "dicom/transfer_syntax.hpp"

namespace htj2k::codec {
namespace {

class BaseSourceDecoder : public ISourceDecoder {
public:
  explicit BaseSourceDecoder(const dicom::LoadedDicom& loaded, dicom::ImageSpec spec)
      : loaded_(loaded), spec_(std::move(spec))
  {
    DcmElement* element = nullptr;
    if (loaded_.dataset->findAndGetElement(DCM_PixelData, element).good()) {
      pixel_data_ = dynamic_cast<DcmPixelData*>(element);
    }
    if (pixel_data_ == nullptr) {
      throw std::runtime_error("PixelData element not found");
    }
  }

  [[nodiscard]] const dicom::ImageSpec& image_spec() const override { return spec_; }
  [[nodiscard]] std::size_t frame_count() const override { return spec_.number_of_frames; }

protected:
  static std::size_t even_length(const std::size_t value) { return (value + 1U) & ~std::size_t{1U}; }

  static void interleave_planar(const std::vector<std::uint8_t>& planar,
                                const dicom::ImageSpec& spec,
                                OwnedFrameBuffer& scratch)
  {
    const auto bytes_per_sample = spec.bytes_per_sample();
    scratch.resize(spec.target_frame_bytes());
    const auto plane_size =
      static_cast<std::size_t>(spec.rows) * static_cast<std::size_t>(spec.columns) * bytes_per_sample;
    for (std::uint32_t y = 0; y < spec.rows; ++y) {
      for (std::uint32_t x = 0; x < spec.columns; ++x) {
        for (std::uint16_t component = 0; component < spec.samples_per_pixel; ++component) {
          const auto src_index =
            static_cast<std::size_t>(component) * plane_size +
            (static_cast<std::size_t>(y) * spec.columns + x) * bytes_per_sample;
          const auto dst_index =
            (static_cast<std::size_t>(y) * spec.columns + x) * spec.samples_per_pixel * bytes_per_sample +
            static_cast<std::size_t>(component) * bytes_per_sample;
          std::memcpy(scratch.bytes.data() + dst_index, planar.data() + src_index, bytes_per_sample);
        }
      }
    }
  }

  static void extract_frame_codestream(DcmDataset& dataset,
                                       DcmPixelData& pixel_data,
                                       const E_TransferSyntax syntax,
                                       const std::size_t frame_index,
                                       const std::size_t frame_count,
                                       std::vector<std::uint8_t>& out)
  {
    DcmPixelSequence* sequence = nullptr;
    if (pixel_data.getEncapsulatedRepresentation(syntax, nullptr, sequence).bad() || sequence == nullptr) {
      throw std::runtime_error("failed to access encapsulated representation");
    }

    std::vector<Uint64> offsets64;
    std::vector<Uint64> lengths64;
    const Uint64* offset_ptr = nullptr;
    unsigned long offset_count = 0;
    if (dataset.findAndGetUint64Array(DCM_ExtendedOffsetTable, offset_ptr, &offset_count).good() &&
        offset_ptr != nullptr && offset_count >= frame_count) {
      offsets64.assign(offset_ptr, offset_ptr + offset_count);
      const Uint64* length_ptr = nullptr;
      unsigned long length_count = 0;
      if (dataset.findAndGetUint64Array(DCM_ExtendedOffsetTableLengths, length_ptr, &length_count).good() &&
          length_ptr != nullptr && length_count >= frame_count) {
        lengths64.assign(length_ptr, length_ptr + length_count);
      }
    }

    if (frame_count == 1 && offsets64.empty()) {
      out.clear();
      for (unsigned long i = 1; i < sequence->card(); ++i) {
        DcmPixelItem* item = nullptr;
        if (sequence->getItem(item, i).bad() || item == nullptr) {
          throw std::runtime_error("failed to access pixel fragment");
        }
        Uint8* data = nullptr;
        if (item->getUint8Array(data).bad() || data == nullptr) {
          continue;
        }
        out.insert(out.end(), data, data + item->getLength());
      }
      return;
    }

    if (!offsets64.empty()) {
      const auto start = offsets64.at(frame_index);
      const auto length =
        !lengths64.empty() ? lengths64.at(frame_index)
                           : ((frame_index + 1 < offsets64.size()) ? offsets64.at(frame_index + 1) - start : Uint64{0});

      Uint64 current_offset = 0;
      out.clear();
      for (unsigned long item_index = 1; item_index < sequence->card(); ++item_index) {
        DcmPixelItem* item = nullptr;
        if (sequence->getItem(item, item_index).bad() || item == nullptr) {
          throw std::runtime_error("failed to access pixel fragment");
        }

        Uint8* data = nullptr;
        if (item->getUint8Array(data).bad() || data == nullptr) {
          continue;
        }
        const auto item_length = static_cast<Uint64>(item->getLength());
        if (current_offset >= start && (length == 0 || current_offset < start + length)) {
          out.insert(out.end(), data, data + item_length);
        }
        current_offset += static_cast<Uint64>(even_length(static_cast<std::size_t>(item_length))) + 8U;
      }
      if (!out.empty()) {
        return;
      }
    }

    const auto fragment_count = sequence->card() > 0 ? sequence->card() - 1U : 0U;
    if (fragment_count == frame_count) {
      DcmPixelItem* item = nullptr;
      if (sequence->getItem(item, static_cast<unsigned long>(frame_index + 1)).bad() || item == nullptr) {
        throw std::runtime_error("failed to access frame fragment");
      }
      Uint8* data = nullptr;
      if (item->getUint8Array(data).bad() || data == nullptr) {
        throw std::runtime_error("missing compressed frame data");
      }
      out.assign(data, data + item->getLength());
      return;
    }

    throw std::runtime_error("cannot determine frame fragments for encapsulated dataset");
  }

  const dicom::LoadedDicom& loaded_;
  dicom::ImageSpec spec_;
  DcmPixelData* pixel_data_ = nullptr;
};

class DcmtkSourceDecoder final : public BaseSourceDecoder {
public:
  explicit DcmtkSourceDecoder(const dicom::LoadedDicom& loaded, dicom::ImageSpec spec)
      : BaseSourceDecoder(loaded, std::move(spec))
  {
    if (loaded_.source_transfer_syntax == EXS_LittleEndianImplicit ||
        loaded_.source_transfer_syntax == EXS_LittleEndianExplicit ||
        loaded_.source_transfer_syntax == EXS_BigEndianExplicit ||
        loaded_.source_transfer_syntax == EXS_DeflatedLittleEndianExplicit) {
      Uint8* raw = nullptr;
      if (pixel_data_->getUint8Array(raw).good() && raw != nullptr) {
        native_pixels_ = raw;
      }
    }
  }

  FrameView decode_frame(const std::size_t index, OwnedFrameBuffer& scratch) override
  {
    if (spec_.photometric_plan.needs_palette_expansion) {
      if (native_pixels_ == nullptr) {
        throw std::runtime_error("PALETTE COLOR requires native pixel access in v1");
      }
      const auto frame_bytes = spec_.source_frame_bytes();
      const auto offset = index * frame_bytes;
      const std::vector<std::uint8_t> input(native_pixels_ + offset, native_pixels_ + offset + frame_bytes);
      dicom::apply_palette_color(
        spec_.palette.red, spec_.palette.green, spec_.palette.blue, spec_.bits_allocated, input, scratch.bytes);
      return scratch.view(spec_.columns * spec_.photometric_plan.target_samples_per_pixel * spec_.bytes_per_sample());
    }

    if (native_pixels_ != nullptr && !spec_.photometric_plan.needs_ybr422_expansion && !spec_.photometric_plan.needs_ybr_to_rgb_conversion &&
        (!spec_.planar_configuration.has_value() || *spec_.planar_configuration == 0)) {
      const auto frame_bytes = spec_.target_frame_bytes();
      const auto frame_offset = frame_bytes * index;
      return FrameView{native_pixels_ + frame_offset, frame_bytes, spec_.columns * spec_.samples_per_pixel * spec_.bytes_per_sample()};
    }

    Uint32 frame_size = 0;
    const auto size_status = pixel_data_->getUncompressedFrameSize(
      loaded_.dataset, frame_size, native_pixels_ != nullptr ? OFTrue : OFFalse);
    if (size_status.bad()) {
      throw std::runtime_error("failed to compute frame size for DCMTK decode");
    }

    scratch.resize(frame_size);
    OFString color_model;
    const auto status = pixel_data_->getUncompressedFrame(
      loaded_.dataset,
      static_cast<Uint32>(index),
      start_fragment_,
      scratch.bytes.data(),
      static_cast<Uint32>(scratch.bytes.size()),
      color_model,
      &cache_);
    if (status.bad()) {
      throw std::runtime_error("DCMTK frame decode failed");
    }

    if (spec_.photometric_plan.needs_ybr422_expansion) {
      std::vector<std::uint8_t> expanded;
      dicom::expand_ybr_full_422(scratch.bytes, static_cast<std::uint16_t>(spec_.columns), static_cast<std::uint16_t>(spec_.rows), expanded);
      scratch.bytes = std::move(expanded);
    }

    if (spec_.planar_configuration.has_value() && *spec_.planar_configuration == 1) {
      const auto planar = scratch.bytes;
      interleave_planar(planar, spec_, scratch);
    }

    if (spec_.photometric_plan.needs_ybr_to_rgb_conversion) {
      if (spec_.bits_allocated != 8) {
        throw std::runtime_error("YBR to RGB conversion currently supports only 8-bit data");
      }
      dicom::ybr_to_rgb_interleaved(scratch.bytes);
    }

    return scratch.view(spec_.columns * spec_.photometric_plan.target_samples_per_pixel * spec_.bytes_per_sample());
  }

private:
  Uint8* native_pixels_ = nullptr;
  Uint32 start_fragment_ = 0;
  DcmFileCache cache_;
};

class OpenJpegSourceDecoder final : public BaseSourceDecoder {
public:
  explicit OpenJpegSourceDecoder(const dicom::LoadedDicom& loaded, dicom::ImageSpec spec)
      : BaseSourceDecoder(loaded, std::move(spec))
  {
  }

  FrameView decode_frame(const std::size_t index, OwnedFrameBuffer& scratch) override
  {
    std::vector<std::uint8_t> codestream;
    extract_frame_codestream(*loaded_.dataset, *pixel_data_, loaded_.source_transfer_syntax, index, frame_count(), codestream);
    decode_openjpeg_frame(codestream, spec_, scratch);
    if (spec_.photometric_plan.needs_ybr_to_rgb_conversion) {
      if (spec_.bits_allocated != 8) {
        throw std::runtime_error("YBR to RGB conversion currently supports only 8-bit data");
      }
      dicom::ybr_to_rgb_interleaved(scratch.bytes);
    }
    return scratch.view(spec_.columns * spec_.photometric_plan.target_samples_per_pixel * spec_.bytes_per_sample());
  }
};

class OpenJphSourceDecoder final : public BaseSourceDecoder {
public:
  explicit OpenJphSourceDecoder(const dicom::LoadedDicom& loaded, dicom::ImageSpec spec)
      : BaseSourceDecoder(loaded, std::move(spec))
  {
  }

  FrameView decode_frame(const std::size_t index, OwnedFrameBuffer& scratch) override
  {
    std::vector<std::uint8_t> codestream;
    extract_frame_codestream(*loaded_.dataset, *pixel_data_, loaded_.source_transfer_syntax, index, frame_count(), codestream);
    decode_openjph_frame(codestream, spec_, scratch);
    if (spec_.photometric_plan.needs_ybr_to_rgb_conversion) {
      if (spec_.bits_allocated != 8) {
        throw std::runtime_error("YBR to RGB conversion currently supports only 8-bit data");
      }
      dicom::ybr_to_rgb_interleaved(scratch.bytes);
    }
    return scratch.view(spec_.columns * spec_.photometric_plan.target_samples_per_pixel * spec_.bytes_per_sample());
  }
};

}  // namespace

std::unique_ptr<ISourceDecoder> create_source_decoder(const dicom::LoadedDicom& loaded, dicom::ImageSpec spec)
{
  if (dicom::supports_dcmtk_frame_decode(loaded.source_transfer_syntax)) {
    return std::unique_ptr<ISourceDecoder>(new DcmtkSourceDecoder(loaded, std::move(spec)));
  }
  if (dicom::is_jpeg2000(loaded.source_transfer_syntax)) {
    return std::unique_ptr<ISourceDecoder>(new OpenJpegSourceDecoder(loaded, std::move(spec)));
  }
  if (dicom::is_htj2k(loaded.source_transfer_syntax)) {
    return std::unique_ptr<ISourceDecoder>(new OpenJphSourceDecoder(loaded, std::move(spec)));
  }
  throw std::runtime_error("unsupported source transfer syntax");
}

}  // namespace htj2k::codec
