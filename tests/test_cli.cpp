#include "tests/test_macros.hpp"

#include "app/cli.hpp"

HTJ2K_TEST(test_cli_defaults)
{
  const char* argv[] = {"transcode_htj2k", "/tmp/input"};
  const auto result = htj2k::app::parse_cli(2, const_cast<char**>(argv));
  HTJ2K_ASSERT_EQ(result.options.input_root.string(), "/tmp/input");
  HTJ2K_ASSERT(!result.options.in_place);
  HTJ2K_ASSERT_EQ(result.options.output_root.string(), "/tmp/input-output");
}

HTJ2K_TEST(test_cli_in_place)
{
  const char* argv[] = {"transcode_htj2k", "/tmp/input", "--in-place", "--workers", "3"};
  const auto result = htj2k::app::parse_cli(5, const_cast<char**>(argv));
  HTJ2K_ASSERT(result.options.in_place);
  HTJ2K_ASSERT_EQ(result.options.workers, 3U);
}
