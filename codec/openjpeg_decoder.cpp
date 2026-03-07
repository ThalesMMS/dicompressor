#include "codec/openjpeg_decoder.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

#include <openjpeg.h>

namespace htj2k::codec {
namespace {

struct MemoryStream {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
};

OPJ_SIZE_T read_callback(void* buffer, OPJ_SIZE_T bytes, void* user_data)
{
  auto* stream = static_cast<MemoryStream*>(user_data);
  const auto remaining = stream->size - stream->offset;
  const auto to_copy = std::min<std::size_t>(remaining, bytes);
  if (to_copy == 0) {
    return static_cast<OPJ_SIZE_T>(-1);
  }
  std::memcpy(buffer, stream->data + stream->offset, to_copy);
  stream->offset += to_copy;
  return to_copy;
}

OPJ_OFF_T skip_callback(OPJ_OFF_T bytes, void* user_data)
{
  auto* stream = static_cast<MemoryStream*>(user_data);
  const auto remaining = static_cast<OPJ_OFF_T>(stream->size - stream->offset);
  const auto to_skip = std::min(bytes, remaining);
  stream->offset += static_cast<std::size_t>(to_skip);
  return to_skip;
}

OPJ_BOOL seek_callback(OPJ_OFF_T bytes, void* user_data)
{
  auto* stream = static_cast<MemoryStream*>(user_data);
  if (bytes < 0 || static_cast<std::size_t>(bytes) > stream->size) {
    return OPJ_FALSE;
  }
  stream->offset = static_cast<std::size_t>(bytes);
  return OPJ_TRUE;
}

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

void decode_openjpeg_frame(const std::vector<std::uint8_t>& codestream,
                           const dicom::ImageSpec& spec,
                           OwnedFrameBuffer& output)
{
  if (spec.bits_allocated == 32 && spec.pixel_representation == 0 && spec.bits_stored >= 32U) {
    throw std::runtime_error("32-bit unsigned full-range JPEG2000 decode is not supported in v1");
  }

  MemoryStream memory{codestream.data(), codestream.size(), 0};
  opj_dparameters_t params;
  opj_set_default_decoder_parameters(&params);

  const bool is_j2k = codestream.size() >= 2 && codestream[0] == 0xFF && codestream[1] == 0x4F;
  opj_codec_t* codec = opj_create_decompress(is_j2k ? OPJ_CODEC_J2K : OPJ_CODEC_JP2);
  if (codec == nullptr) {
    throw std::runtime_error("failed to create OpenJPEG decoder");
  }

  opj_stream_t* stream = opj_stream_create(4096, OPJ_TRUE);
  if (stream == nullptr) {
    opj_destroy_codec(codec);
    throw std::runtime_error("failed to create OpenJPEG stream");
  }

  opj_stream_set_read_function(stream, read_callback);
  opj_stream_set_skip_function(stream, skip_callback);
  opj_stream_set_seek_function(stream, seek_callback);
  opj_stream_set_user_data(stream, &memory, nullptr);
  opj_stream_set_user_data_length(stream, codestream.size());

  opj_image_t* image = nullptr;
  if (!opj_setup_decoder(codec, &params) || !opj_read_header(stream, codec, &image) ||
      !opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) {
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    if (image != nullptr) {
      opj_image_destroy(image);
    }
    throw std::runtime_error("OpenJPEG decode failed");
  }

  if (image == nullptr || image->numcomps < spec.photometric_plan.target_samples_per_pixel) {
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    if (image != nullptr) {
      opj_image_destroy(image);
    }
    throw std::runtime_error("OpenJPEG returned unexpected component count");
  }

  const auto bytes_per_sample = spec.bytes_per_sample();
  output.resize(spec.target_frame_bytes());
  for (std::uint32_t y = 0; y < spec.rows; ++y) {
    for (std::uint32_t x = 0; x < spec.columns; ++x) {
      for (std::uint32_t component = 0; component < spec.photometric_plan.target_samples_per_pixel; ++component) {
        const auto& comp = image->comps[component];
        const auto sample = comp.data[y * spec.columns + x];
        const auto pixel_index =
          (static_cast<std::size_t>(y) * spec.columns + x) * spec.photometric_plan.target_samples_per_pixel + component;
        pack_sample(output.bytes, pixel_index * bytes_per_sample, sample, bytes_per_sample);
      }
    }
  }

  opj_image_destroy(image);
  opj_stream_destroy(stream);
  opj_destroy_codec(codec);
}

}  // namespace htj2k::codec
