#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import multiprocessing
import os
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List, Sequence

import numpy as np
from pydicom import dcmread
from pydicom.dataset import FileMetaDataset
from pydicom.encaps import encapsulate_extended
from pydicom.misc import is_dicom
from pydicom.pixels import iter_pixels
from pydicom.uid import PYDICOM_IMPLEMENTATION_UID, UID, generate_uid

HTJ2K_LOSSLESS_UID = UID("1.2.840.10008.1.2.4.201")
EXPLICIT_VR_LE_UID = UID("1.2.840.10008.1.2.1")

# Common transfer syntaxes that indicate the source has already undergone a
# lossy step. This matters because re-encoding to lossless HTJ2K cannot recover
# information previously lost.
KNOWN_LOSSY_TRANSFER_SYNTAXES = {
    "1.2.840.10008.1.2.4.50",   # JPEG Baseline
    "1.2.840.10008.1.2.4.51",   # JPEG Extended
    "1.2.840.10008.1.2.4.81",   # JPEG-LS Near-lossless
    "1.2.840.10008.1.2.4.100",  # MPEG2 Main Profile / Main Level
    "1.2.840.10008.1.2.4.101",  # MPEG2 Main Profile / High Level
    "1.2.840.10008.1.2.4.102",  # MPEG-4 AVC/H.264 High Profile / Level 4.1
    "1.2.840.10008.1.2.4.103",  # MPEG-4 AVC/H.264 BD-compatible High Profile / Level 4.1
    "1.2.840.10008.1.2.4.104",  # MPEG-4 AVC/H.264 High Profile / Level 4.2 For 2D Video
    "1.2.840.10008.1.2.4.105",  # MPEG-4 AVC/H.264 High Profile / Level 4.2 For 3D Video
    "1.2.840.10008.1.2.4.106",  # MPEG-4 AVC/H.264 Stereo High Profile / Level 4.2
    "1.2.840.10008.1.2.4.107",  # HEVC/H.265 Main Profile / Level 5.1
    "1.2.840.10008.1.2.4.108",  # HEVC/H.265 Main 10 Profile / Level 5.1
}

SUPPORTED_PHOTOMETRIC = {
    "MONOCHROME1",
    "MONOCHROME2",
    "PALETTE COLOR",
    "RGB",
    "YBR_FULL",
    "YBR_FULL_422",
}


class ProcessingError(RuntimeError):
    pass


class UnsupportedFormatError(ProcessingError):
    """Raised when a DICOM file cannot be HTJ2K-encoded (e.g. BitsAllocated=1, no PixelData)."""
    pass


@dataclass(frozen=True)
class ImageSpec:
    rows: int
    cols: int
    samples_per_pixel: int
    bits_allocated: int
    bits_stored: int
    high_bit: int
    pixel_representation: int
    photometric_interpretation: str
    number_of_frames: int
    target_photometric_interpretation: str


@dataclass
class ProcessResult:
    src: str
    dst: str | None
    status: str
    message: str
    patient: str | None


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Recursively re-encodes DICOM files to HTJ2K lossless "
            "(Transfer Syntax UID 1.2.840.10008.1.2.4.201) using OpenJPH."
        )
    )
    parser.add_argument("input_root", help="Input root folder, for example ./Studies")

    mode = parser.add_mutually_exclusive_group(required=False)
    mode.add_argument(
        "--output-root",
        help=(
            "Output root folder. If omitted and --in-place is not used, defaults to "
            "<input_root>-output"
        ),
    )
    mode.add_argument(
        "--in-place",
        action="store_true",
        help="Replaces each original file with the generated HTJ2K.",
    )

    parser.add_argument(
        "--zip-per-patient",
        action="store_true",
        help="After processing, generates one ZIP per patient folder at the first level.",
    )
    parser.add_argument(
        "--zip-mode",
        choices=("stored", "deflated"),
        default="stored",
        help=(
            "ZIP mode. Since DICOMs will already be compressed in HTJ2K, "
            "'stored' is usually faster and almost always sufficient."
        ),
    )
    parser.add_argument(
        "--report-json",
        default=None,
        help="Optional path to write JSON report with successes/failures.",
    )
    parser.add_argument(
        "--ojph-compress",
        default=None,
        help="Path to ojph_compress executable. If omitted, searches in PATH and common locations.",
    )
    parser.add_argument(
        "--gdcmconv",
        default=None,
        help="Path to gdcmconv executable. If omitted, searches in PATH and common locations.",
    )
    parser.add_argument(
        "--num-decomps",
        type=int,
        default=5,
        help="Maximum number of OpenJPH wavelet decompositions (default: 5).",
    )
    parser.add_argument(
        "--block-size",
        default="64,64",
        help="OpenJPH codeblock size, in x,y format (default: 64,64).",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrites existing output files in --output-root mode.",
    )
    parser.add_argument(
        "--regenerate-sop-instance-uid",
        action="store_true",
        help=(
            "Generates a new SOP Instance UID for the output. Useful if you want a "
            "more conservative provenance policy."
        ),
    )
    parser.add_argument(
        "--strict-color",
        action="store_true",
        help=(
            "Fails instead of silently skipping unsupported photometries. "
            "Without this option, such files are marked as failed in the report and the rest continues."
        ),
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=None,
        help=(
            "Number of parallel processes. Default: number of system CPUs "
            f"(detected: {os.cpu_count() or 1}). Use 1 to disable parallelism."
        ),
    )

    return parser.parse_args(argv)


def find_executable(preferred: str | None, names: Sequence[str]) -> str:
    candidates: List[str] = []
    if preferred:
        candidates.append(preferred)
    for name in names:
        which = shutil.which(name)
        if which:
            candidates.append(which)
    candidates.extend(
        [
            "/opt/homebrew/bin/ojph_compress",
            "/usr/local/bin/ojph_compress",
            "/opt/homebrew/bin/gdcmconv",
            "/usr/local/bin/gdcmconv",
        ]
    )

    seen = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if candidate and Path(candidate).exists() and os.access(candidate, os.X_OK):
            return candidate

    raise FileNotFoundError(f"Executable not found. Attempts: {', '.join(candidates) if candidates else '(none)'}")


def validate_roots(input_root: Path, output_root: Path | None, in_place: bool) -> None:
    if not input_root.exists() or not input_root.is_dir():
        raise ProcessingError(f"Input folder does not exist or is not a directory: {input_root}")

    if in_place:
        return

    assert output_root is not None
    output_root_resolved = output_root.resolve()
    input_root_resolved = input_root.resolve()
    try:
        output_root_resolved.relative_to(input_root_resolved)
    except ValueError:
        return
    raise ProcessingError(
        "The output folder cannot be inside the input folder, to avoid recursion over the generated files themselves."
    )


def discover_dicom_files(input_root: Path) -> List[Path]:
    files: List[Path] = []
    scanned = 0
    for path in input_root.rglob("*"):
        if not path.is_file():
            continue
        scanned += 1
        if scanned % 50000 == 0:
            print(f"  Discovering files... {scanned} checked, {len(files)} DICOM so far", flush=True)
        if path.suffix.lower() == ".dcm":
            files.append(path)
            continue
        try:
            if is_dicom(str(path)):
                files.append(path)
        except Exception:
            continue
    files.sort()
    return files


def get_patient_name(input_root: Path, file_path: Path) -> str | None:
    rel = file_path.relative_to(input_root)
    if not rel.parts:
        return None
    return rel.parts[0]


def dataset_transfer_syntax_uid(ds) -> UID | None:
    try:
        value = ds.file_meta.TransferSyntaxUID
    except Exception:
        return None
    try:
        return UID(str(value))
    except Exception:
        return None


def is_compressed_transfer_syntax(ds) -> bool:
    tsuid = dataset_transfer_syntax_uid(ds)
    if tsuid is None:
        return False
    try:
        return bool(tsuid.is_compressed)
    except Exception:
        return False


def source_is_historically_lossy(ds) -> bool:
    if str(ds.get("LossyImageCompression", "")).strip() == "01":
        return True

    tsuid = dataset_transfer_syntax_uid(ds)
    if tsuid is None:
        return False
    return str(tsuid) in KNOWN_LOSSY_TRANSFER_SYNTAXES


def normalize_block_size(value: str) -> str:
    raw = value.strip().replace("{", "").replace("}", "")
    parts = [p.strip() for p in raw.split(",") if p.strip()]
    if len(parts) != 2:
        raise ProcessingError("--block-size must be in x,y format; example: 64,64")
    x, y = parts
    if not x.isdigit() or not y.isdigit():
        raise ProcessingError("--block-size must contain only positive integers")
    return f"{{{x},{y}}}"


def build_image_spec(ds) -> ImageSpec:
    if "PixelData" not in ds:
        raise UnsupportedFormatError("Dataset without PixelData")
    if "FloatPixelData" in ds or "DoubleFloatPixelData" in ds:
        raise UnsupportedFormatError("Float Pixel Data / Double Float Pixel Data are not supported by this script")

    required = [
        "Rows",
        "Columns",
        "SamplesPerPixel",
        "BitsAllocated",
        "BitsStored",
        "HighBit",
        "PixelRepresentation",
        "PhotometricInterpretation",
    ]
    missing = [name for name in required if name not in ds]
    if missing:
        raise ProcessingError(f"Dataset without required pixel attributes: {', '.join(missing)}")

    rows = int(ds.Rows)
    cols = int(ds.Columns)
    samples = int(ds.SamplesPerPixel)
    bits_allocated = int(ds.BitsAllocated)
    bits_stored = int(ds.BitsStored)
    high_bit = int(ds.HighBit)
    pixel_representation = int(ds.PixelRepresentation)
    number_of_frames = int(ds.get("NumberOfFrames", 1))
    photometric = str(ds.PhotometricInterpretation).strip().upper()

    if rows <= 0 or cols <= 0:
        raise ProcessingError("Invalid Rows/Columns")
    if samples not in (1, 3):
        raise UnsupportedFormatError(f"SamplesPerPixel={samples} not supported; supported: 1 or 3")
    if bits_allocated not in (8, 16, 32):
        raise UnsupportedFormatError(
            f"BitsAllocated={bits_allocated} not supported by this script; supported: 8, 16 or 32"
        )
    if photometric not in SUPPORTED_PHOTOMETRIC:
        raise UnsupportedFormatError(
            "PhotometricInterpretation not conservatively supported by this script: "
            f"{photometric}. Supported: {', '.join(sorted(SUPPORTED_PHOTOMETRIC))}"
        )

    target_photometric = photometric
    if photometric == "YBR_FULL_422":
        # iter_pixels(raw=True) expands 4:2:2 subsampling to full resolution,
        # therefore the output should be written as YBR_FULL.
        target_photometric = "YBR_FULL"

    return ImageSpec(
        rows=rows,
        cols=cols,
        samples_per_pixel=samples,
        bits_allocated=bits_allocated,
        bits_stored=bits_stored,
        high_bit=high_bit,
        pixel_representation=pixel_representation,
        photometric_interpretation=photometric,
        number_of_frames=number_of_frames,
        target_photometric_interpretation=target_photometric,
    )


def ensure_little_endian_contiguous(arr: np.ndarray) -> np.ndarray:
    if arr.dtype.byteorder == ">":
        arr = arr.byteswap().newbyteorder("<")
    elif arr.dtype.byteorder == "=":
        # Native-endian; on Apple Silicon this is already little-endian, but keep explicit.
        arr = arr.astype(arr.dtype.newbyteorder("<"), copy=False)
    elif arr.dtype.byteorder == "|":
        # byte-sized types
        pass
    return np.ascontiguousarray(arr)


def safe_num_decomps(rows: int, cols: int, requested: int) -> int:
    min_dim = max(1, min(rows, cols))
    max_decomps = int(math.floor(math.log2(min_dim))) if min_dim > 1 else 0
    return max(0, min(requested, max_decomps))


def run_command(cmd: Sequence[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        list(cmd),
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        joined = " ".join(cmd)
        raise ProcessingError(
            f"Command failed ({proc.returncode}): {joined}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
        )
    return proc


def decode_frames(input_path: Path, ds, gdcmconv_path: str) -> Iterator[np.ndarray]:
    def native_generator(path: Path) -> Iterator[np.ndarray]:
        yield from iter_pixels(str(path), raw=True)

    gen = native_generator(input_path)
    try:
        first = next(gen)
    except StopIteration:
        return
    except Exception as first_exc:
        if not is_compressed_transfer_syntax(ds):
            raise ProcessingError(f"Failed to decode Pixel Data: {first_exc}") from first_exc

        with tempfile.TemporaryDirectory(prefix="gdcm-raw-") as tmp_dir:
            tmp_path = Path(tmp_dir) / "uncompressed.dcm"
            run_command([gdcmconv_path, "--raw", "--explicit", str(input_path), str(tmp_path)])
            fallback_gen = native_generator(tmp_path)
            try:
                first_fb = next(fallback_gen)
            except StopIteration:
                return
            except Exception as second_exc:
                raise ProcessingError(
                    "Failed to decode the file both directly and via gdcmconv --raw. "
                    f"Direct error: {first_exc!r}; fallback error: {second_exc!r}"
                ) from second_exc

            yield first_fb
            for frame in fallback_gen:
                yield frame
            return

    yield first
    for frame in gen:
        yield frame


def frame_to_openjph_input(frame: np.ndarray, spec: ImageSpec, workdir: Path, frame_index: int) -> tuple[Path, list[str]]:
    if frame.dtype.kind not in ("u", "i"):
        raise ProcessingError(f"dtype not supported for lossless compression: {frame.dtype}")

    frame = ensure_little_endian_contiguous(frame)
    signed = spec.pixel_representation == 1
    signed_word = "true" if signed else "false"
    dims = f"{{{spec.cols},{spec.rows}}}"

    if spec.samples_per_pixel == 1:
        if frame.ndim != 2:
            raise ProcessingError(f"Expected 2D mono frame, received shape={frame.shape}")
        raw_path = workdir / f"frame-{frame_index:06d}.raw"
        raw_path.write_bytes(frame.tobytes(order="C"))
        extra = [
            "-dims", dims,
            "-num_comps", "1",
            "-signed", signed_word,
            "-bit_depth", str(spec.bits_stored),
            "-downsamp", "{1,1}",
        ]
        return raw_path, extra

    if frame.ndim != 3 or frame.shape[2] != 3:
        raise ProcessingError(f"Expected 3D RGB/YBR frame with 3 channels, received shape={frame.shape}")

    planar = np.transpose(frame, (2, 0, 1)).copy(order="C")
    yuv_path = workdir / f"frame-{frame_index:06d}.yuv"
    yuv_path.write_bytes(planar.tobytes(order="C"))
    extra = [
        "-dims", dims,
        "-num_comps", "3",
        "-signed", f"{signed_word},{signed_word},{signed_word}",
        "-bit_depth", f"{spec.bits_stored},{spec.bits_stored},{spec.bits_stored}",
        "-downsamp", "{1,1},{1,1},{1,1}",
    ]
    return yuv_path, extra


def encode_frame_openjph(
    frame: np.ndarray,
    spec: ImageSpec,
    frame_index: int,
    workdir: Path,
    ojph_compress_path: str,
    num_decomps_requested: int,
    block_size: str,
) -> bytes:
    input_path, format_args = frame_to_openjph_input(frame, spec, workdir, frame_index)
    output_codestream = workdir / f"frame-{frame_index:06d}.j2c"

    num_decomps = safe_num_decomps(spec.rows, spec.cols, num_decomps_requested)

    cmd = [
        ojph_compress_path,
        "-i", str(input_path),
        "-o", str(output_codestream),
        "-reversible", "true",
        "-num_decomps", str(num_decomps),
        "-prog_order", "LRCP",
        "-block_size", block_size,
        *format_args,
    ]

    run_command(cmd)
    return output_codestream.read_bytes()


def ensure_file_meta(ds) -> None:
    if getattr(ds, "file_meta", None) is None:
        ds.file_meta = FileMetaDataset()

    if "FileMetaInformationVersion" not in ds.file_meta:
        ds.file_meta.FileMetaInformationVersion = b"\x00\x01"

    if "SOPClassUID" in ds:
        ds.file_meta.MediaStorageSOPClassUID = ds.SOPClassUID
    if "SOPInstanceUID" in ds:
        ds.file_meta.MediaStorageSOPInstanceUID = ds.SOPInstanceUID

    ds.file_meta.TransferSyntaxUID = HTJ2K_LOSSLESS_UID
    ds.file_meta.ImplementationClassUID = PYDICOM_IMPLEMENTATION_UID
    ds.file_meta.ImplementationVersionName = "PYHTJ2K1"


def update_dataset_for_htj2k(ds, spec: ImageSpec, historically_lossy: bool, regenerate_uid: bool) -> None:
    ds.is_little_endian = True
    ds.is_implicit_VR = False

    if regenerate_uid:
        ds.SOPInstanceUID = generate_uid()

    if "SOPInstanceUID" in ds:
        ds.file_meta.MediaStorageSOPInstanceUID = ds.SOPInstanceUID
    if "SOPClassUID" in ds:
        ds.file_meta.MediaStorageSOPClassUID = ds.SOPClassUID

    ds.file_meta.TransferSyntaxUID = HTJ2K_LOSSLESS_UID
    ds.PhotometricInterpretation = spec.target_photometric_interpretation
    ds.BitsAllocated = spec.bits_allocated
    ds.BitsStored = spec.bits_stored
    ds.HighBit = spec.high_bit
    ds.PixelRepresentation = spec.pixel_representation
    ds.Rows = spec.rows
    ds.Columns = spec.cols
    ds.SamplesPerPixel = spec.samples_per_pixel

    if spec.samples_per_pixel > 1:
        ds.PlanarConfiguration = 0
    elif "PlanarConfiguration" in ds:
        del ds.PlanarConfiguration

    if historically_lossy:
        ds.LossyImageCompression = "01"


def _copy_file_as_is(src: Path, dst: Path) -> None:
    """Copy a DICOM file without modification when it cannot be HTJ2K-encoded."""
    if src.resolve() == dst.resolve():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def process_file(
    src: Path,
    dst: Path,
    ojph_compress_path: str,
    gdcmconv_path: str,
    num_decomps: int,
    block_size: str,
    overwrite: bool,
    regenerate_uid: bool,
) -> str:
    """Returns 'ok' if HTJ2K-encoded, 'copied' if copied as-is due to unsupported format."""
    if dst.exists() and not overwrite and src.resolve() != dst.resolve():
        raise ProcessingError(f"Output file already exists: {dst}")

    ds = dcmread(str(src), force=True, defer_size=1024)
    try:
        spec = build_image_spec(ds)
    except UnsupportedFormatError:
        _copy_file_as_is(src, dst)
        return "copied"
    historically_lossy = source_is_historically_lossy(ds)

    encoded_frames: list[bytes] = []
    decoded_count = 0
    with tempfile.TemporaryDirectory(prefix="htj2k-work-") as tmp_dir_str:
        tmp_dir = Path(tmp_dir_str)
        for frame_index, frame in enumerate(decode_frames(src, ds, gdcmconv_path)):
            encoded = encode_frame_openjph(
                frame=frame,
                spec=spec,
                frame_index=frame_index,
                workdir=tmp_dir,
                ojph_compress_path=ojph_compress_path,
                num_decomps_requested=num_decomps,
                block_size=block_size,
            )
            encoded_frames.append(encoded)
            decoded_count += 1

    if decoded_count != spec.number_of_frames:
        raise ProcessingError(
            f"Number of decoded frames ({decoded_count}) differs from NumberOfFrames ({spec.number_of_frames})"
        )

    ensure_file_meta(ds)
    update_dataset_for_htj2k(ds, spec, historically_lossy, regenerate_uid)

    pixel_data, ext_offsets, ext_lengths = encapsulate_extended(encoded_frames)
    ds.PixelData = pixel_data
    ds.ExtendedOffsetTable = ext_offsets
    ds.ExtendedOffsetTableLengths = ext_lengths
    ds["PixelData"].VR = "OB"
    ds["PixelData"].is_undefined_length = True

    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp_output = dst.parent / f".{dst.name}.tmp-htj2k"
    ds.save_as(str(tmp_output), write_like_original=False)

    if src.exists() and src.resolve() == dst.resolve():
        shutil.copymode(src, tmp_output)
        os.replace(tmp_output, dst)
    else:
        if dst.exists():
            dst.unlink()
        os.replace(tmp_output, dst)

    return "ok"


def _process_file_worker(
    src: str,
    dst: str,
    patient: str | None,
    ojph_compress_path: str,
    gdcmconv_path: str,
    num_decomps: int,
    block_size: str,
    overwrite: bool,
    regenerate_uid: bool,
) -> ProcessResult:
    """Top-level worker callable for ProcessPoolExecutor."""
    try:
        status = process_file(
            src=Path(src),
            dst=Path(dst),
            ojph_compress_path=ojph_compress_path,
            gdcmconv_path=gdcmconv_path,
            num_decomps=num_decomps,
            block_size=block_size,
            overwrite=overwrite,
            regenerate_uid=regenerate_uid,
        )
        return ProcessResult(
            src=src,
            dst=dst,
            status=status,
            message=status,
            patient=patient,
        )
    except Exception as exc:
        return ProcessResult(
            src=src,
            dst=dst,
            status="failed",
            message=str(exc),
            patient=patient,
        )


def zip_patient_folder(patient_dir: Path, zip_path: Path, mode: str) -> None:
    compression = zipfile.ZIP_STORED if mode == "stored" else zipfile.ZIP_DEFLATED
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, mode="w", compression=compression, allowZip64=True) as zf:
        empty_dirs: list[Path] = []
        for root, dirs, files in os.walk(patient_dir):
            root_path = Path(root)
            rel_root = root_path.relative_to(patient_dir.parent)
            if not dirs and not files:
                empty_dirs.append(rel_root)
            for filename in files:
                file_path = root_path / filename
                if file_path.resolve() == zip_path.resolve():
                    continue
                arcname = file_path.relative_to(patient_dir.parent)
                zf.write(file_path, arcname)
        for rel_dir in empty_dirs:
            info = zipfile.ZipInfo(str(rel_dir).rstrip("/") + "/")
            zf.writestr(info, b"")


def build_destination(input_root: Path, src: Path, output_root: Path | None, in_place: bool) -> Path:
    if in_place:
        return src
    assert output_root is not None
    return output_root / src.relative_to(input_root)


def write_report(report_path: Path, results: list[ProcessResult]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "summary": {
            "total": len(results),
            "ok": sum(1 for r in results if r.status == "ok"),
            "copied": sum(1 for r in results if r.status == "copied"),
            "failed": sum(1 for r in results if r.status == "failed"),
        },
        "results": [r.__dict__ for r in results],
    }
    report_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])

    input_root = Path(args.input_root).resolve()
    output_root = None if args.in_place else (Path(args.output_root).resolve() if args.output_root else (input_root.parent / f"{input_root.name}-output").resolve())

    try:
        validate_roots(input_root, output_root, args.in_place)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    try:
        ojph_compress_path = find_executable(args.ojph_compress, ["ojph_compress"])
    except Exception as exc:
        print(f"ERROR: could not locate ojph_compress: {exc}", file=sys.stderr)
        return 2

    try:
        gdcmconv_path = find_executable(args.gdcmconv, ["gdcmconv"])
    except Exception as exc:
        print(f"ERROR: could not locate gdcmconv: {exc}", file=sys.stderr)
        return 2

    try:
        block_size = normalize_block_size(args.block_size)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if output_root is not None:
        output_root.mkdir(parents=True, exist_ok=True)

    files = discover_dicom_files(input_root)
    if not files:
        print("No DICOM files found.", file=sys.stderr)
        return 1

    results: list[ProcessResult] = []
    patients_processed: set[str] = set()

    num_workers = args.workers if args.workers is not None else (os.cpu_count() or 1)
    num_workers = max(1, num_workers)

    print(f"Input: {input_root}")
    if args.in_place:
        print("Mode: in-place")
    else:
        print(f"Output: {output_root}")
    print(f"DICOM files found: {len(files)}")
    print(f"Workers: {num_workers}")
    print(f"OpenJPH: {ojph_compress_path}")
    print(f"GDCM: {gdcmconv_path}")

    # Start timing
    start_time = time.time()

    # Build work items
    work_items = []
    for src in files:
        patient = get_patient_name(input_root, src)
        dst = build_destination(input_root, src, output_root, args.in_place)
        work_items.append((src, dst, patient))

    if num_workers == 1:
        # Sequential path — preserves strict_color abort and avoids multiprocessing overhead
        for index, (src, dst, patient) in enumerate(work_items, start=1):
            try:
                status = process_file(
                    src=src,
                    dst=dst,
                    ojph_compress_path=ojph_compress_path,
                    gdcmconv_path=gdcmconv_path,
                    num_decomps=args.num_decomps,
                    block_size=block_size,
                    overwrite=args.overwrite,
                    regenerate_uid=args.regenerate_sop_instance_uid,
                )
                result = ProcessResult(
                    src=str(src), dst=str(dst), status=status, message=status, patient=patient,
                )
            except Exception as exc:
                result = ProcessResult(
                    src=str(src), dst=str(dst), status="failed", message=str(exc), patient=patient,
                )
            results.append(result)
            if result.status in ("ok", "copied") and result.patient:
                patients_processed.add(result.patient)
            elif result.status == "failed":
                print(f"[{index}/{len(files)}] FAILED  {src}\n    {result.message}", file=sys.stderr)
                if args.strict_color:
                    if "PhotometricInterpretation" in result.message or "SamplesPerPixel" in result.message:
                        break
            if index % 1000 == 0 or index == len(files):
                elapsed = time.time() - start_time
                rate = index / elapsed if elapsed > 0 else 0
                print(f"  [{index}/{len(files)}] {rate:.1f} files/s", flush=True)
    else:
        # Parallel path using ProcessPoolExecutor
        completed = 0
        aborted = False
        # Use 'spawn' context explicitly for macOS safety (fork can deadlock with certain libs)
        mp_context = multiprocessing.get_context("spawn")

        with ProcessPoolExecutor(max_workers=num_workers, mp_context=mp_context) as executor:
            future_to_info = {}
            for src, dst, patient in work_items:
                fut = executor.submit(
                    _process_file_worker,
                    src=str(src),
                    dst=str(dst),
                    patient=patient,
                    ojph_compress_path=ojph_compress_path,
                    gdcmconv_path=gdcmconv_path,
                    num_decomps=args.num_decomps,
                    block_size=block_size,
                    overwrite=args.overwrite,
                    regenerate_uid=args.regenerate_sop_instance_uid,
                )
                future_to_info[fut] = (src, dst, patient)

            for fut in as_completed(future_to_info):
                completed += 1
                try:
                    result = fut.result()
                except Exception as exc:
                    src, dst, patient = future_to_info[fut]
                    result = ProcessResult(
                        src=str(src), dst=str(dst), status="failed",
                        message=f"Unexpected worker error: {exc}", patient=patient,
                    )
                results.append(result)
                if result.status in ("ok", "copied") and result.patient:
                    patients_processed.add(result.patient)
                elif result.status == "failed":
                    print(
                        f"[{completed}/{len(files)}] FAILED  {result.src}\n    {result.message}",
                        file=sys.stderr,
                    )
                    if args.strict_color and not aborted:
                        if "PhotometricInterpretation" in result.message or "SamplesPerPixel" in result.message:
                            aborted = True
                            # Cancel pending futures
                            for pending_fut in future_to_info:
                                pending_fut.cancel()

                if completed % 1000 == 0 or completed == len(files):
                    elapsed = time.time() - start_time
                    rate = completed / elapsed if elapsed > 0 else 0
                    print(f"  [{completed}/{len(files)}] {rate:.1f} files/s", flush=True)

    if args.zip_per_patient:
        zip_root = input_root if args.in_place else output_root
        assert zip_root is not None
        for patient in sorted(patients_processed):
            patient_dir = zip_root / patient
            if not patient_dir.exists() or not patient_dir.is_dir():
                continue
            zip_path = zip_root / f"{patient}.zip"
            try:
                zip_patient_folder(patient_dir, zip_path, args.zip_mode)
                if not args.in_place:
                    shutil.rmtree(patient_dir)
                print(f"ZIP created: {zip_path}")
            except Exception as exc:
                results.append(
                    ProcessResult(
                        src=str(patient_dir),
                        dst=str(zip_path),
                        status="failed",
                        message=f"Failed to zip patient folder: {exc}",
                        patient=patient,
                    )
                )
                print(f"FAILED to create ZIP of {patient_dir}: {exc}", file=sys.stderr)

    # End timing
    end_time = time.time()
    execution_time = end_time - start_time

    # Print execution time and throughput
    hours = int(execution_time // 3600)
    minutes = int((execution_time % 3600) // 60)
    seconds = execution_time % 60

    if hours > 0:
        print(f"Execution time: {hours}h {minutes}m {seconds:.2f}s")
    elif minutes > 0:
        print(f"Execution time: {minutes}m {seconds:.2f}s")
    else:
        print(f"Execution time: {seconds:.2f}s")

    if execution_time > 0:
        files_per_sec = len(results) / execution_time
        print(f"Throughput: {files_per_sec:.1f} files/s")

    if args.report_json:
        write_report(Path(args.report_json).resolve(), results)

    ok_count = sum(1 for r in results if r.status == "ok")
    copied_count = sum(1 for r in results if r.status == "copied")
    fail_count = sum(1 for r in results if r.status == "failed")
    print(f"Summary: ok={ok_count} copied={copied_count} failed={fail_count} total={len(results)}")
    return 0 if fail_count == 0 else 3


if __name__ == "__main__":
    raise SystemExit(main())
