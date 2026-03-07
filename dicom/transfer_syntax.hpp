#pragma once

#include <string>

#include <dcmtk/dcmdata/dcxfer.h>

namespace htj2k::dicom {

constexpr const char* kHtj2kLosslessUid = "1.2.840.10008.1.2.4.201";

[[nodiscard]] bool is_encapsulated(E_TransferSyntax syntax);
[[nodiscard]] bool is_htj2k(E_TransferSyntax syntax);
[[nodiscard]] bool is_jpeg2000(E_TransferSyntax syntax);
[[nodiscard]] bool is_lossy_transfer_syntax(E_TransferSyntax syntax);
[[nodiscard]] bool supports_dcmtk_frame_decode(E_TransferSyntax syntax);
[[nodiscard]] std::string transfer_syntax_uid(E_TransferSyntax syntax);
[[nodiscard]] E_TransferSyntax parse_transfer_syntax_uid(const std::string& uid);

}  // namespace htj2k::dicom
