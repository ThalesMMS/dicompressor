# DICOM_NOTES

## Saída

- Transfer Syntax de saída sempre `1.2.840.10008.1.2.4.201`
- File Meta refeito com `EWM_createNewMeta`

## UIDs

- `SOPInstanceUID` preservado por default
- regenerado apenas com `--regenerate-sop-instance-uid`

## LossyImageCompression

- Mantém `"01"` se o dataset já traz `"01"`
- Mantém `"01"` se a sintaxe de origem é historicamente lossy
- O HTJ2K final é lossless apenas em relação ao buffer decodificado

## Fotometria

- `MONOCHROME1/2`: preservadas
- `RGB`: preservada
- `YBR_FULL`: preservada
- `YBR_FULL_422`: expandida e gravada como `YBR_FULL`
- `PALETTE COLOR`: expandida para `RGB`
- `YBR_RCT` e `YBR_ICT`: convertidas para `RGB`

## Pixel Data

- Encapsulado em `DcmPixelSequence`
- Um fragmento por frame na v1
- Basic Offset Table vazio
- `ExtendedOffsetTable` e `ExtendedOffsetTableLengths` em multi-frame

## Limitações

- `FloatPixelData` e `DoubleFloatPixelData`: fora do escopo da v1
- `BitsAllocated = 1`: fallback `copied`
- 32-bit unsigned full-range: não suportado na v1
- Overlays em bits não usados não são preservados explicitamente
- Sequências encapsuladas sem BOT/EOT e sem heurística clara entram em `copied`
