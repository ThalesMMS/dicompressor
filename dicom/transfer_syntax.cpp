#include "dicom/transfer_syntax.hpp"

#include <unordered_set>

#include <dcmtk/dcmdata/dcuid.h>

namespace htj2k::dicom {

bool is_encapsulated(const E_TransferSyntax syntax)
{
  return DcmXfer(syntax).usesEncapsulatedFormat();
}

bool is_htj2k(const E_TransferSyntax syntax)
{
  return syntax == EXS_HighThroughputJPEG2000LosslessOnly ||
         syntax == EXS_HighThroughputJPEG2000withRPCLOptionsLosslessOnly ||
         syntax == EXS_HighThroughputJPEG2000;
}

bool is_jpeg2000(const E_TransferSyntax syntax)
{
  return syntax == EXS_JPEG2000LosslessOnly || syntax == EXS_JPEG2000 ||
         syntax == EXS_JPEG2000MulticomponentLosslessOnly || syntax == EXS_JPEG2000Multicomponent;
}

bool is_lossy_transfer_syntax(const E_TransferSyntax syntax)
{
  return DcmXfer(syntax).isPixelDataLossyCompressed();
}

bool supports_dcmtk_frame_decode(const E_TransferSyntax syntax)
{
  return syntax == EXS_LittleEndianImplicit || syntax == EXS_LittleEndianExplicit ||
         syntax == EXS_BigEndianExplicit || syntax == EXS_DeflatedLittleEndianExplicit ||
         syntax == EXS_JPEGProcess1 || syntax == EXS_JPEGProcess2_4 || syntax == EXS_JPEGProcess14 ||
         syntax == EXS_JPEGProcess14SV1 || syntax == EXS_JPEGLSLossless || syntax == EXS_JPEGLSLossy ||
         syntax == EXS_RLELossless;
}

std::string transfer_syntax_uid(const E_TransferSyntax syntax)
{
  return DcmXfer(syntax).getXferID();
}

E_TransferSyntax parse_transfer_syntax_uid(const std::string& uid)
{
  return DcmXfer(uid.c_str()).getXfer();
}

}  // namespace htj2k::dicom
