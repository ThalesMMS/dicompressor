#include "tests/test_macros.hpp"

#include <filesystem>
#include <fstream>

#include "core/patient_zipper.hpp"

HTJ2K_TEST(test_zip_per_patient)
{
  const auto root = std::filesystem::temp_directory_path() / "htj2k-zip-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "PatientA" / "Study1");
  std::ofstream(root / "PatientA" / "Study1" / "image.dcm") << "dicom";

  htj2k::ZipOptions options;
  options.enabled = true;
  options.mode = htj2k::ZipMode::stored;
  const auto results = htj2k::core::zip_patients(root, options);
  HTJ2K_ASSERT_EQ(results.size(), 1U);
  HTJ2K_ASSERT(results.front().ok);
  HTJ2K_ASSERT(std::filesystem::exists(root / "PatientA.zip"));
  std::filesystem::remove_all(root);
}
