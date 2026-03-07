# transcode_htj2k

Transcoder DICOM em C++20 para recodificação em lote para HTJ2K lossless (`1.2.840.10008.1.2.4.201`), com hot path 100% nativo, sem subprocess e sem arquivos temporários `.raw/.yuv/.j2c`.

## Visão geral

- Encode HTJ2K sempre via OpenJPH em memória.
- Stack DICOM principal via DCMTK para leitura P10, dataset, metadados, encapsulamento e escrita.
- Decode de origem:
  - nativo/JPEG/JPEG-LS/RLE via DCMTK
  - JPEG 2000 via OpenJPEG
  - HTJ2K via OpenJPH
- Paralelismo principal por arquivo com fila dinâmica e thread pool fixo.
- Escrita segura com arquivo temporário vizinho + `rename` atômico.
- Modo `--output-root` e `--in-place`.
- ZIP por paciente com backend `miniz`.

## Estrutura

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

## Estado atual

Implementado nesta versão:

- CLI completa.
- Descoberta recursiva de DICOMs.
- Recodificação de single-frame e multi-frame.
- `MONOCHROME1`, `MONOCHROME2`, `RGB`, `YBR_FULL`, `YBR_FULL_422`, `PALETTE COLOR`, `YBR_RCT`, `YBR_ICT`.
- Preservação conservadora de metadados e de histórico lossy.
- `ExtendedOffsetTable` e `ExtendedOffsetTableLengths` para multi-frame de saída.
- Relatório agregado e JSON.
- ZIP por paciente.
- Tests + benchmark executable.

Limitações conhecidas estão em [DICOM_NOTES.md](./DICOM_NOTES.md).

## Dependências

Obrigatórias:

- CMake 3.24+
- Ninja
- OpenJPH 0.26.x
- OpenJPEG 2.5.x
- DCMTK 3.7.x
- compilador C++20

Os scripts em [`scripts/`](./scripts) montam um prefixo isolado em `.deps/install/...`.

## Build

### 1. Bootstrap de dependências

macOS Apple Silicon:

```bash
./scripts/bootstrap_deps_macos_arm64.sh
```

Linux x86_64:

```bash
./scripts/bootstrap_deps_linux_x86_64.sh
```

### 2. Configurar

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

### 3. Compilar

```bash
cmake --build --preset release -j
```

### 4. Testar

```bash
ctest --preset release
```

## Presets

- `release`
- `debug-sanitized`
- `macos-arm64-release`

Release usa `-O3`, `NDEBUG` e IPO/LTO quando suportado.

## Uso

```bash
transcode_htj2k <input_root> [--output-root PATH | --in-place]
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

### Exemplos

Saída espelhada em pasta separada:

```bash
./build/release/transcode_htj2k ./Exames --output-root ./Exames-output
```

In-place:

```bash
./build/release/transcode_htj2k ./Exames --in-place --workers 8
```

Com relatório JSON:

```bash
./build/release/transcode_htj2k ./Exames \
  --output-root ./Exames-output \
  --report-json ./report.json
```

Com ZIP por paciente:

```bash
./build/release/transcode_htj2k ./Exames \
  --output-root ./Exames-output \
  --zip-per-patient \
  --zip-mode stored
```

## Comportamento funcional

- Sempre grava saída em HTJ2K lossless.
- Reencoda arquivos já comprimidos quando a sintaxe de origem é decodificável.
- Preserva `SOPInstanceUID` por padrão.
- Com `--regenerate-sop-instance-uid`, gera novo UID e deixa o file meta coerente.
- Se o dataset já indica perda anterior ou a Transfer Syntax de origem é lossy, mantém `LossyImageCompression = "01"`.
- Arquivos não suportados:
  - `--output-root`: são copiados e marcados como `copied`
  - `--in-place`: original fica intacto e entra como `copied`
- `--strict-color` promove casos de cor/fotometria não suportados para falha.

## Relatório

O relatório final agrega:

- `total`
- `ok`
- `copied`
- `failed`
- `zipped`
- `frames`
- `pixels`
- `bytes_read`
- `bytes_written`
- tempos por fase
- throughput em `files/s`, `frames/s` e `MPix/s`

Com `--report-json`, o arquivo também inclui entradas por job.

## Benchmark

O binário [`transcode_bench`](./bench/main.cpp) reutiliza o mesmo core e emite o resumo agregado da execução:

```bash
./build/release/transcode_bench ./Exames --output-root ./Exames-bench-out --workers 1
./build/release/transcode_bench ./Exames --output-root ./Exames-bench-out --workers 8
```

## Testes

A suíte cobre:

- parsing de CLI
- descoberta de DICOM
- round-trip mínimo do encoder/decoder HTJ2K
- geração de report JSON
- `output-root`
- `in-place`
- fallback `copied`
- ZIP por paciente

## Notas

- O protótipo Python original permanece no repositório apenas como referência arquitetural do fluxo substituído.
- Esta v1 prioriza throughput e segurança operacional sobre cobertura universal de todos os formatos DICOM exóticos.
- Detalhes de performance: [PERFORMANCE_NOTES.md](./PERFORMANCE_NOTES.md)
- Detalhes DICOM: [DICOM_NOTES.md](./DICOM_NOTES.md)
