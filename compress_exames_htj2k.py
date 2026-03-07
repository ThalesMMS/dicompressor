#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
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
            "Recodifica recursivamente arquivos DICOM para HTJ2K lossless "
            "(Transfer Syntax UID 1.2.840.10008.1.2.4.201) usando OpenJPH."
        )
    )
    parser.add_argument("input_root", help="Pasta raiz de entrada, por exemplo ./Exames")

    mode = parser.add_mutually_exclusive_group(required=False)
    mode.add_argument(
        "--output-root",
        help=(
            "Pasta raiz de saída. Se omitido e --in-place não for usado, o padrão é "
            "<input_root>-output"
        ),
    )
    mode.add_argument(
        "--in-place",
        action="store_true",
        help="Substitui cada arquivo original pelo HTJ2K gerado.",
    )

    parser.add_argument(
        "--zip-per-patient",
        action="store_true",
        help="Após o processamento, gera um ZIP por pasta de paciente no primeiro nível.",
    )
    parser.add_argument(
        "--zip-mode",
        choices=("stored", "deflated"),
        default="stored",
        help=(
            "Modo do ZIP. Como os DICOMs já estarão comprimidos em HTJ2K, "
            "'stored' costuma ser mais rápido e quase sempre suficiente."
        ),
    )
    parser.add_argument(
        "--report-json",
        default=None,
        help="Caminho opcional para gravar relatório JSON com sucessos/falhas.",
    )
    parser.add_argument(
        "--ojph-compress",
        default=None,
        help="Caminho do executável ojph_compress. Se omitido, procura no PATH e em locais comuns.",
    )
    parser.add_argument(
        "--gdcmconv",
        default=None,
        help="Caminho do executável gdcmconv. Se omitido, procura no PATH e em locais comuns.",
    )
    parser.add_argument(
        "--num-decomps",
        type=int,
        default=5,
        help="Número máximo de decomposições wavelet do OpenJPH (padrão: 5).",
    )
    parser.add_argument(
        "--block-size",
        default="64,64",
        help="Tamanho do codeblock do OpenJPH, no formato x,y (padrão: 64,64).",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Sobrescreve arquivos de saída existentes no modo --output-root.",
    )
    parser.add_argument(
        "--regenerate-sop-instance-uid",
        action="store_true",
        help=(
            "Gera um novo SOP Instance UID para a saída. Útil se você quiser uma política "
            "mais conservadora de proveniência."
        ),
    )
    parser.add_argument(
        "--strict-color",
        action="store_true",
        help=(
            "Falha em vez de pular silenciosamente fotometrias não suportadas. "
            "Sem esta opção, tais arquivos entram como falha no relatório e o restante continua."
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

    raise FileNotFoundError(f"Executável não encontrado. Tentativas: {', '.join(candidates) if candidates else '(nenhuma)'}")


def validate_roots(input_root: Path, output_root: Path | None, in_place: bool) -> None:
    if not input_root.exists() or not input_root.is_dir():
        raise ProcessingError(f"Pasta de entrada não existe ou não é diretório: {input_root}")

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
        "A pasta de saída não pode ficar dentro da pasta de entrada, para evitar recursão sobre os próprios arquivos gerados."
    )


def mirror_directory_tree(input_root: Path, output_root: Path) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    for entry in input_root.rglob("*"):
        if entry.is_dir():
            (output_root / entry.relative_to(input_root)).mkdir(parents=True, exist_ok=True)


def discover_dicom_files(input_root: Path) -> List[Path]:
    files: List[Path] = []
    for path in sorted(input_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() == ".dcm":
            files.append(path)
            continue
        try:
            if is_dicom(str(path)):
                files.append(path)
        except Exception:
            continue
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
        raise ProcessingError("--block-size deve estar no formato x,y; exemplo: 64,64")
    x, y = parts
    if not x.isdigit() or not y.isdigit():
        raise ProcessingError("--block-size deve conter apenas inteiros positivos")
    return f"{{{x},{y}}}"


def build_image_spec(ds) -> ImageSpec:
    if "PixelData" not in ds:
        raise ProcessingError("Dataset sem PixelData")
    if "FloatPixelData" in ds or "DoubleFloatPixelData" in ds:
        raise ProcessingError("Float Pixel Data / Double Float Pixel Data não são suportados por este script")

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
        raise ProcessingError(f"Dataset sem atributos obrigatórios de pixel: {', '.join(missing)}")

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
        raise ProcessingError("Rows/Columns inválidos")
    if samples not in (1, 3):
        raise ProcessingError(f"SamplesPerPixel={samples} não suportado; suportado: 1 ou 3")
    if bits_allocated not in (8, 16, 32):
        raise ProcessingError(
            f"BitsAllocated={bits_allocated} não suportado por este script; suportado: 8, 16 ou 32"
        )
    if photometric not in SUPPORTED_PHOTOMETRIC:
        raise ProcessingError(
            "PhotometricInterpretation não suportado de forma conservadora por este script: "
            f"{photometric}. Suportados: {', '.join(sorted(SUPPORTED_PHOTOMETRIC))}"
        )

    target_photometric = photometric
    if photometric == "YBR_FULL_422":
        # iter_pixels(raw=True) expande o subsampling 4:2:2 para resolução total,
        # portanto a saída deve ser gravada como YBR_FULL.
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
            f"Comando falhou ({proc.returncode}): {joined}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
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
            raise ProcessingError(f"Falha ao decodificar Pixel Data: {first_exc}") from first_exc

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
                    "Falha ao decodificar o arquivo tanto diretamente quanto via gdcmconv --raw. "
                    f"Erro direto: {first_exc!r}; erro fallback: {second_exc!r}"
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
        raise ProcessingError(f"dtype não suportado para compressão lossless: {frame.dtype}")

    frame = ensure_little_endian_contiguous(frame)
    signed = spec.pixel_representation == 1
    signed_word = "true" if signed else "false"
    dims = f"{{{spec.cols},{spec.rows}}}"

    if spec.samples_per_pixel == 1:
        if frame.ndim != 2:
            raise ProcessingError(f"Frame mono esperado em 2D, recebido shape={frame.shape}")
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
        raise ProcessingError(f"Frame RGB/YBR esperado em 3D com 3 canais, recebido shape={frame.shape}")

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


def process_file(
    src: Path,
    dst: Path,
    ojph_compress_path: str,
    gdcmconv_path: str,
    num_decomps: int,
    block_size: str,
    overwrite: bool,
    regenerate_uid: bool,
) -> None:
    if dst.exists() and not overwrite and src.resolve() != dst.resolve():
        raise ProcessingError(f"Arquivo de saída já existe: {dst}")

    ds = dcmread(str(src), force=True, defer_size=1024)
    spec = build_image_spec(ds)
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
            f"Número de frames decodificados ({decoded_count}) diferente de NumberOfFrames ({spec.number_of_frames})"
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
        print(f"ERRO: {exc}", file=sys.stderr)
        return 2

    try:
        ojph_compress_path = find_executable(args.ojph_compress, ["ojph_compress"])
    except Exception as exc:
        print(f"ERRO: não consegui localizar ojph_compress: {exc}", file=sys.stderr)
        return 2

    try:
        gdcmconv_path = find_executable(args.gdcmconv, ["gdcmconv"])
    except Exception as exc:
        print(f"ERRO: não consegui localizar gdcmconv: {exc}", file=sys.stderr)
        return 2

    try:
        block_size = normalize_block_size(args.block_size)
    except Exception as exc:
        print(f"ERRO: {exc}", file=sys.stderr)
        return 2

    if output_root is not None:
        mirror_directory_tree(input_root, output_root)

    files = discover_dicom_files(input_root)
    if not files:
        print("Nenhum arquivo DICOM encontrado.", file=sys.stderr)
        return 1

    results: list[ProcessResult] = []
    patients_processed: set[str] = set()

    print(f"Entrada: {input_root}")
    if args.in_place:
        print("Modo: in-place")
    else:
        print(f"Saída: {output_root}")
    print(f"Arquivos DICOM encontrados: {len(files)}")
    print(f"OpenJPH: {ojph_compress_path}")
    print(f"GDCM: {gdcmconv_path}")

    # Start timing
    start_time = time.time()

    for index, src in enumerate(files, start=1):
        patient = get_patient_name(input_root, src)
        dst = build_destination(input_root, src, output_root, args.in_place)
        try:
            process_file(
                src=src,
                dst=dst,
                ojph_compress_path=ojph_compress_path,
                gdcmconv_path=gdcmconv_path,
                num_decomps=args.num_decomps,
                block_size=block_size,
                overwrite=args.overwrite,
                regenerate_uid=args.regenerate_sop_instance_uid,
            )
            results.append(
                ProcessResult(
                    src=str(src),
                    dst=str(dst),
                    status="ok",
                    message="ok",
                    patient=patient,
                )
            )
            if patient:
                patients_processed.add(patient)
            # print(f"[{index}/{len(files)}] OK     {src}")
        except Exception as exc:
            message = str(exc)
            results.append(
                ProcessResult(
                    src=str(src),
                    dst=str(dst),
                    status="failed",
                    message=message,
                    patient=patient,
                )
            )
            print(f"[{index}/{len(files)}] FALHA  {src}\n    {message}", file=sys.stderr)
            if args.strict_color:
                # strict-color is meant as a hard-stop toggle when a color case or other
                # unsupported input should abort the batch immediately.
                if "PhotometricInterpretation" in message or "SamplesPerPixel" in message:
                    break

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
                print(f"ZIP criado: {zip_path}")
            except Exception as exc:
                results.append(
                    ProcessResult(
                        src=str(patient_dir),
                        dst=str(zip_path),
                        status="failed",
                        message=f"Falha ao zipar pasta do paciente: {exc}",
                        patient=patient,
                    )
                )
                print(f"FALHA ao criar ZIP de {patient_dir}: {exc}", file=sys.stderr)

    # End timing
    end_time = time.time()
    execution_time = end_time - start_time

    # Print execution time
    hours = int(execution_time // 3600)
    minutes = int((execution_time % 3600) // 60)
    seconds = execution_time % 60

    if hours > 0:
        print(f"Tempo de execução: {hours}h {minutes}m {seconds:.2f}s")
    elif minutes > 0:
        print(f"Tempo de execução: {minutes}m {seconds:.2f}s")
    else:
        print(f"Tempo de execução: {seconds:.2f}s")

    if args.report_json:
        write_report(Path(args.report_json).resolve(), results)

    ok_count = sum(1 for r in results if r.status == "ok")
    fail_count = sum(1 for r in results if r.status == "failed")
    print(f"Resumo: ok={ok_count} falha={fail_count} total={len(results)}")
    return 0 if fail_count == 0 else 3


if __name__ == "__main__":
    raise SystemExit(main())
