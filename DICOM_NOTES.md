# DICOM_NOTES

## Output

- Output Transfer Syntax always `1.2.840.10008.1.2.4.201`
- File Meta remade with `EWM_createNewMeta`

## UIDs

- `SOPInstanceUID` preserved by default
- regenerated only with `--regenerate-sop-instance-uid`

## LossyImageCompression

- Maintains `"01"` if the dataset already has `"01"`
- Maintains `"01"` if the source syntax is historically lossy
- The final HTJ2K is lossless only in relation to the decoded buffer

## Photometry

- `MONOCHROME1/2`: preserved
- `RGB`: preserved
- `YBR_FULL`: preserved
- `YBR_FULL_422`: expanded and written as `YBR_FULL`
- `PALETTE COLOR`: expanded to `RGB`
- `YBR_RCT` and `YBR_ICT`: converted to `RGB`

## Pixel Data

- Encapsulated in `DcmPixelSequence`
- One fragment per frame in v1
- Empty Basic Offset Table
- `ExtendedOffsetTable` and `ExtendedOffsetTableLengths` in multi-frame

## Limitations

- `FloatPixelData` and `DoubleFloatPixelData`: out of scope for v1
- `BitsAllocated = 1`: fallback `copied`
- 32-bit unsigned full-range: not supported in v1
- Overlays in unused bits are not explicitly preserved
- Encapsulated sequences without BOT/EOT and without clear heuristics enter as `copied`
