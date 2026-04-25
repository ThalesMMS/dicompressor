#include "tests/test_macros.hpp"

#include "app/cli.hpp"

#include <stdexcept>
#include <string>

HTJ2K_TEST(test_cli_defaults)
{
  const char* argv[] = {"dicompressor", "/tmp/input"};
  const auto result = htj2k::app::parse_cli(2, const_cast<char**>(argv));
  HTJ2K_ASSERT_EQ(result.options.input_root.string(), "/tmp/input");
  HTJ2K_ASSERT(!result.options.in_place);
  HTJ2K_ASSERT_EQ(result.options.output_root.string(), "/tmp/input-output");
}

HTJ2K_TEST(test_cli_in_place)
{
  const char* argv[] = {"dicompressor", "/tmp/input", "--in-place", "--workers", "3"};
  const auto result = htj2k::app::parse_cli(5, const_cast<char**>(argv));
  HTJ2K_ASSERT(result.options.in_place);
  HTJ2K_ASSERT_EQ(result.options.workers, 3U);
}

HTJ2K_TEST(test_cli_zip_mode_requires_zip_per_patient)
{
  const char* argv[] = {"dicompressor", "/tmp/input", "--zip-mode", "deflated"};

  try {
    (void)htj2k::app::parse_cli(4, const_cast<char**>(argv));
  } catch (const std::runtime_error& ex) {
    HTJ2K_ASSERT_EQ(std::string(ex.what()), "--zip-mode requires --zip-per-patient");
    return;
  }

  throw std::runtime_error("expected --zip-mode without --zip-per-patient to fail");
}

HTJ2K_TEST(test_cli_zip_mode_with_zip_per_patient)
{
  const char* argv[] = {
    "dicompressor", "/tmp/input", "--zip-per-patient", "--zip-mode", "deflated"};
  const auto result = htj2k::app::parse_cli(5, const_cast<char**>(argv));

  HTJ2K_ASSERT(result.options.zip.enabled);
  HTJ2K_ASSERT_EQ(result.options.zip.mode, htj2k::ZipMode::deflated);
}
