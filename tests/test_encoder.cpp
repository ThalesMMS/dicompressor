#include "tests/test_macros.hpp"

#include <numeric>

#include "codec/htj2k_encoder.hpp"
#include "codec/openjph_source_decoder.hpp"
#include "dicom/dicom_metadata.hpp"

HTJ2K_TEST(test_htj2k_encoder_roundtrip_mono)
{
  htj2k::dicom::ImageSpec spec;
  spec.rows = 8;
  spec.columns = 8;
  spec.samples_per_pixel = 1;
  spec.bits_allocated = 8;
  spec.bits_stored = 8;
  spec.high_bit = 7;
  spec.pixel_representation = 0;
  spec.photometric_interpretation = "MONOCHROME2";
  spec.photometric_plan = htj2k::dicom::plan_photometric("MONOCHROME2", 1, false);

  htj2k::codec::OwnedFrameBuffer input;
  input.resize(spec.target_frame_bytes());
  std::iota(input.bytes.begin(), input.bytes.end(), std::uint8_t{0});

  htj2k::codec::Htj2kEncoder encoder;
  const auto encoded = encoder.encode(spec, input.view(spec.columns), htj2k::EncodeOptions{});

  htj2k::codec::OwnedFrameBuffer decoded;
  htj2k::codec::decode_openjph_frame(encoded.codestream, spec, decoded);
  HTJ2K_ASSERT_EQ(decoded.bytes, input.bytes);
}
