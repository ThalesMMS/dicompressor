#include "dicom/dicom_metadata.hpp"

#include <stdexcept>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcuid.h>

#include "dicom/transfer_syntax.hpp"

namespace htj2k::dicom {
namespace {

template <typename T>
void require(OFCondition condition, const T& message)
{
  if (condition.bad()) {
    throw std::runtime_error(std::string(message));
  }
}

std::string get_required_string(DcmDataset& dataset, const DcmTagKey& tag, const char* label)
{
  OFString value;
  require(dataset.findAndGetOFString(tag, value), label);
  return value.c_str();
}

std::uint16_t get_required_u16(DcmDataset& dataset, const DcmTagKey& tag, const char* label)
{
  Uint16 value = 0;
  require(dataset.findAndGetUint16(tag, value), label);
  return value;
}

std::uint32_t get_number_of_frames(DcmDataset& dataset)
{
  Sint32 frames = 1;
  const auto status = dataset.findAndGetSint32(DCM_NumberOfFrames, frames);
  if (status.bad() || frames < 1) {
    return 1;
  }
  return static_cast<std::uint32_t>(frames);
}

bool historical_lossy(DcmDataset& dataset, const E_TransferSyntax source_syntax)
{
  OFString lossy;
  if (dataset.findAndGetOFString(DCM_LossyImageCompression, lossy).good() && lossy == "01") {
    return true;
  }
  return is_lossy_transfer_syntax(source_syntax);
}

std::vector<std::uint16_t> read_palette_lut(DcmDataset& dataset, const DcmTagKey&, const DcmTagKey& data_tag)
{
  const Uint16* lut_data = nullptr;
  unsigned long count = 0;
  if (dataset.findAndGetUint16Array(data_tag, lut_data, &count).bad() || lut_data == nullptr) {
    return {};
  }
  std::vector<std::uint16_t> lut(count);
  for (unsigned long i = 0; i < count; ++i) {
    lut[i] = lut_data[i];
  }
  return lut;
}

}  // namespace

ImageSpec extract_image_spec(DcmDataset& dataset, const E_TransferSyntax source_syntax)
{
  ImageSpec spec;
  spec.photometric_interpretation = get_required_string(dataset, DCM_PhotometricInterpretation, "missing PhotometricInterpretation");
  spec.samples_per_pixel = get_required_u16(dataset, DCM_SamplesPerPixel, "missing SamplesPerPixel");
  spec.rows = get_required_u16(dataset, DCM_Rows, "missing Rows");
  spec.columns = get_required_u16(dataset, DCM_Columns, "missing Columns");
  spec.bits_allocated = get_required_u16(dataset, DCM_BitsAllocated, "missing BitsAllocated");
  spec.bits_stored = get_required_u16(dataset, DCM_BitsStored, "missing BitsStored");
  spec.high_bit = get_required_u16(dataset, DCM_HighBit, "missing HighBit");
  spec.pixel_representation = get_required_u16(dataset, DCM_PixelRepresentation, "missing PixelRepresentation");
  spec.number_of_frames = get_number_of_frames(dataset);
  spec.historically_lossy = historical_lossy(dataset, source_syntax);
  spec.has_pixel_data = dataset.tagExists(DCM_PixelData);
  spec.is_float = dataset.tagExists(DCM_FloatPixelData);
  spec.is_double = dataset.tagExists(DCM_DoubleFloatPixelData);

  if (spec.samples_per_pixel > 1) {
    Uint16 planar = 0;
    if (dataset.findAndGetUint16(DCM_PlanarConfiguration, planar).good()) {
      spec.planar_configuration = planar;
    }
  }

  spec.photometric_plan = plan_photometric(spec.photometric_interpretation, spec.samples_per_pixel, false);
  spec.target_photometric_interpretation = spec.photometric_plan.target_photometric;

  if (spec.photometric_plan.needs_palette_expansion) {
    spec.palette.red = read_palette_lut(dataset, DCM_RedPaletteColorLookupTableDescriptor, DCM_RedPaletteColorLookupTableData);
    spec.palette.green = read_palette_lut(dataset, DCM_GreenPaletteColorLookupTableDescriptor, DCM_GreenPaletteColorLookupTableData);
    spec.palette.blue = read_palette_lut(dataset, DCM_BluePaletteColorLookupTableDescriptor, DCM_BluePaletteColorLookupTableData);
  }

  return spec;
}

void apply_output_metadata(DcmDataset& dataset, const ImageSpec& spec, const MetadataUpdatePlan& plan)
{
  if (plan.regenerate_sop_instance_uid) {
    char uid[100];
    dcmGenerateUniqueIdentifier(uid, SITE_INSTANCE_UID_ROOT);
    dataset.putAndInsertString(DCM_SOPInstanceUID, uid);
  }

  dataset.putAndInsertString(DCM_PhotometricInterpretation, spec.target_photometric_interpretation.c_str());
  dataset.putAndInsertUint16(DCM_SamplesPerPixel, spec.photometric_plan.target_samples_per_pixel);
  if (spec.photometric_plan.target_planar_configuration.has_value()) {
    dataset.putAndInsertUint16(DCM_PlanarConfiguration, *spec.photometric_plan.target_planar_configuration);
  } else {
    dataset.findAndDeleteElement(DCM_PlanarConfiguration, true, true);
  }

  if (spec.photometric_plan.needs_palette_expansion) {
    dataset.findAndDeleteElement(DCM_RedPaletteColorLookupTableDescriptor, true, true);
    dataset.findAndDeleteElement(DCM_GreenPaletteColorLookupTableDescriptor, true, true);
    dataset.findAndDeleteElement(DCM_BluePaletteColorLookupTableDescriptor, true, true);
    dataset.findAndDeleteElement(DCM_RedPaletteColorLookupTableData, true, true);
    dataset.findAndDeleteElement(DCM_GreenPaletteColorLookupTableData, true, true);
    dataset.findAndDeleteElement(DCM_BluePaletteColorLookupTableData, true, true);
    dataset.findAndDeleteElement(DCM_SegmentedRedPaletteColorLookupTableData, true, true);
    dataset.findAndDeleteElement(DCM_SegmentedGreenPaletteColorLookupTableData, true, true);
    dataset.findAndDeleteElement(DCM_SegmentedBluePaletteColorLookupTableData, true, true);
  }

  if (spec.historically_lossy) {
    dataset.putAndInsertString(DCM_LossyImageCompression, "01");
  }
}

}  // namespace htj2k::dicom
