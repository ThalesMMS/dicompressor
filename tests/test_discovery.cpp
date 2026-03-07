#include "tests/test_macros.hpp"

#include <fstream>
#include <filesystem>

#include "core/file_discovery.hpp"

HTJ2K_TEST(test_discovery_ignores_non_dicom)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-discovery-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "patient");
  {
    std::ofstream(root / "file.txt") << "hello";
    std::ofstream dcm(root / "patient" / "scan.dcm", std::ios::binary);
    std::string preamble(128, '\0');
    dcm.write(preamble.data(), static_cast<std::streamsize>(preamble.size()));
    dcm.write("DICM", 4);
  }

  const auto result = htj2k::core::discover_files(root);
  HTJ2K_ASSERT_EQ(result.files.size(), 1U);
  HTJ2K_ASSERT_EQ(result.files.front().generic_string(), "patient/scan.dcm");
  std::filesystem::remove_all(root);
}
