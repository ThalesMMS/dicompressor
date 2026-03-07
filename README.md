# dicompressor

DICOM transcoder in C++20 for batch re-encoding to HTJ2K lossless (`1.2.840.10008.1.2.4.201`), with 100% native hot path, no subprocess and no temporary `.raw/.yuv/.j2c` files.

## Overview

- HTJ2K encode always via OpenJPH in memory.
- Main DICOM stack via DCMTK for P10 reading, dataset, metadata, encapsulation and writing.
- Source decode:
  - native/JPEG/JPEG-LS/RLE via DCMTK
  - JPEG 2000 via OpenJPEG
  - HTJ2K via OpenJPH
- Main parallelism per file with dynamic queue and fixed thread pool.
- Safe writing with neighbor temporary file + atomic `rename`.
- `--output-root` and `--in-place` modes.
- ZIP per patient with `miniz` backend.

## Structure

```text
app/
core/
codec/
dicom/
util/
platform/
tests/
bench/
cmake/
scripts/
third_party/miniz/
```

## Current state

Implemented in this version:

- Complete CLI.
- Recursive DICOM discovery.
- Single-frame and multi-frame re-encoding.
- `MONOCHROME1`, `MONOCHROME2`, `RGB`, `YBR_FULL`, `YBR_FULL_422`, `PALETTE COLOR`, `YBR_RCT`, `YBR_ICT`.
- Conservative preservation of metadata and lossy history.
- `ExtendedOffsetTable` and `ExtendedOffsetTableLengths` for multi-frame output.
- Aggregate and JSON report.
- ZIP per patient.
- Tests + benchmark executable.

Known limitations are in [DICOM_NOTES.md](./DICOM_NOTES.md).

## Dependencies

Required:

- CMake 3.24+
- Ninja
- OpenJPH 0.26.x
- OpenJPEG 2.5.x
- DCMTK 3.7.x
- C++20 compiler

The scripts in [`scripts/`](./scripts) build an isolated prefix in `.deps/install/...`.

## Build

### 1. Bootstrap dependencies

macOS Apple Silicon:

```bash
./scripts/bootstrap_deps_macos_arm64.sh
```

Linux x86_64:

```bash
./scripts/bootstrap_deps_linux_x86_64.sh
```

### 2. Configure

macOS Apple Silicon:

```bash
cmake --preset macos-arm64-release \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/install/macos-arm64"
```

Linux:

```bash
cmake --preset release \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/install/linux-x86_64"
```

### 3. Compile

```bash
cmake --build --preset release -j
```

### 4. Test

```bash
ctest --preset release
```

## Presets

- `release`
- `debug-sanitized`
- `macos-arm64-release`

Release uses `-O3`, `NDEBUG` and IPO/LTO when supported.

## Usage

```bash
dicompressor <input_root> [--output-root PATH | --in-place]
                           [--zip-per-patient]
                           [--zip-mode stored|deflated]
                           [--report-json PATH]
                           [--num-decomps N]
                           [--block-size X,Y]
                           [--overwrite]
                           [--regenerate-sop-instance-uid]
                           [--strict-color]
                           [--workers N]
                           [--log-level trace|debug|info|warn|error]
```

### Examples

Mirrored output in separate folder:

```bash
./build/release/dicompressor ./Studies --output-root ./Studies-output
```

In-place:

```bash
./build/release/dicompressor ./Studies --in-place --workers 8
```

With JSON report:

```bash
./build/release/dicompressor ./Studies \
  --output-root ./Studies-output \
  --report-json ./report.json
```

With ZIP per patient:

```bash
./build/release/dicompressor ./Studies \
  --output-root ./Studies-output \
  --zip-per-patient \
  --zip-mode stored
```

## Functional behavior

- Always writes output in HTJ2K lossless.
- Re-encodes already compressed files when the source syntax is decodable.
- Preserves `SOPInstanceUID` by default.
- With `--regenerate-sop-instance-uid`, generates new UID and keeps file meta coherent.
- If the dataset already indicates prior loss or the source Transfer Syntax is lossy, maintains `LossyImageCompression = "01"`.
- Unsupported files:
  - `--output-root`: are copied and marked as `copied`
  - `--in-place`: original remains intact and enters as `copied`
- `--strict-color` promotes unsupported color/photometry cases to failure.

## Report

The final report aggregates:

- `total`
- `ok`
- `copied`
- `failed`
- `zipped`
- `frames`
- `pixels`
- `bytes_read`
- `bytes_written`
- times per phase
- throughput in `files/s`, `frames/s` and `MPix/s`

With `--report-json`, the file also includes entries per job.

## Benchmark

The [`transcode_bench`](./bench/main.cpp) binary reuses the same core and emits the aggregate execution summary:

```bash
./build/release/transcode_bench ./Studies --output-root ./Studies-bench-out --workers 1
./build/release/transcode_bench ./Studies --output-root ./Studies-bench-out --workers 8
```

## Tests

The suite covers:

- CLI parsing
- DICOM discovery
- minimal HTJ2K encoder/decoder round-trip
- JSON report generation
- `output-root`
- `in-place`
- `copied` fallback
- ZIP per patient

## Notes

- The original Python prototype remains in the repository only as architectural reference for the replaced flow.
- This v1 prioritizes throughput and operational safety over universal coverage of all exotic DICOM formats.
- Performance details: [PERFORMANCE_NOTES.md](./PERFORMANCE_NOTES.md)
- DICOM details: [DICOM_NOTES.md](./DICOM_NOTES.md)
