#include "tests/test_macros.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dicom/streaming_pixel_sequence_builder.hpp"

namespace {

std::vector<std::uint8_t> frame_bytes(const std::size_t length)
{
  std::vector<std::uint8_t> bytes(length);
  for (std::size_t i = 0; i < length; ++i) {
    bytes[i] = static_cast<std::uint8_t>(i & 0xFFU);
  }
  return bytes;
}

Uint64 padded_length(const std::size_t length)
{
  return static_cast<Uint64>((length + 1U) & ~std::size_t{1U});
}

}  // namespace

HTJ2K_TEST(test_streaming_pixel_sequence_builder_empty)
{
  htj2k::dicom::StreamingPixelSequenceBuilder builder;

  auto result = builder.finalize();
  HTJ2K_ASSERT(result.pixel_sequence != nullptr);
  HTJ2K_ASSERT_EQ(result.pixel_sequence->card(), 1UL);
  HTJ2K_ASSERT(result.extended_offsets.empty());
  HTJ2K_ASSERT(result.extended_lengths.empty());
}

HTJ2K_TEST(test_streaming_pixel_sequence_builder_single_frame_offset)
{
  htj2k::dicom::StreamingPixelSequenceBuilder builder;
  const auto frame = frame_bytes(13);
  builder.append_frame(frame);

  auto result = builder.finalize();
  HTJ2K_ASSERT(result.pixel_sequence != nullptr);
  HTJ2K_ASSERT_EQ(result.pixel_sequence->card(), 2UL);
  HTJ2K_ASSERT_EQ(result.extended_offsets.size(), 1U);
  HTJ2K_ASSERT_EQ(result.extended_offsets[0], Uint64{0});
  HTJ2K_ASSERT_EQ(result.extended_lengths.size(), 1U);
  HTJ2K_ASSERT_EQ(result.extended_lengths[0], padded_length(13));
}

HTJ2K_TEST(test_streaming_pixel_sequence_builder_multiframe_spacing)
{
  htj2k::dicom::StreamingPixelSequenceBuilder builder;
  const std::vector<std::size_t> lengths{17, 22, 19, 28};

  Uint64 current_offset = 0;
  std::vector<Uint64> expected_offsets;
  std::vector<Uint64> expected_lengths;
  for (const auto length : lengths) {
    expected_offsets.push_back(current_offset);
    expected_lengths.push_back(padded_length(length));
    current_offset += padded_length(length) + htj2k::dicom::kFragmentItemHeaderSize;
    const auto frame = frame_bytes(length);
    const std::span<const std::uint8_t> frame_view{frame.data(), frame.size()};
    builder.append_frame(frame_view);
  }

  auto result = builder.finalize();
  HTJ2K_ASSERT(result.pixel_sequence != nullptr);
  HTJ2K_ASSERT_EQ(result.pixel_sequence->card(), static_cast<unsigned long>(lengths.size() + 1U));
  HTJ2K_ASSERT(result.extended_offsets == expected_offsets);
  HTJ2K_ASSERT(result.extended_lengths == expected_lengths);
}

HTJ2K_TEST(test_streaming_pixel_sequence_builder_lengths_are_even)
{
  htj2k::dicom::StreamingPixelSequenceBuilder builder;
  for (std::size_t length = 1; length <= 9; ++length) {
    builder.append_frame(frame_bytes(length));
  }

  auto result = builder.finalize();
  for (const auto length : result.extended_lengths) {
    HTJ2K_ASSERT_EQ(length % 2U, Uint64{0});
  }
}

HTJ2K_TEST(test_streaming_pixel_sequence_builder_finalize_is_single_use)
{
  htj2k::dicom::StreamingPixelSequenceBuilder builder;
  builder.append_frame(frame_bytes(8));
  (void)builder.finalize();

  bool threw = false;
  try {
    (void)builder.finalize();
  } catch (const std::logic_error&) {
    threw = true;
  }
  HTJ2K_ASSERT(threw);
}
