# Recodificação DICOM para HTJ2K lossless (OpenJPH + GDCM + Python)

Este projeto percorre uma árvore como:

```text
./Exames/
./Exames/Fulano/data-exame-1/*.dcm
./Exames/Fulano/data-exame-2/*.dcm
./Exames/Ciclano/data-exame-1/*.dcm
./Exames/Beltrano/
```

e recodifica todos os DICOMs para **HTJ2K lossless**.

## Arquitetura

- **OpenJPH (`ojph_compress`)**: encoder HTJ2K principal.
- **GDCM (`gdcmconv`)**: fallback de descompressão para sintaxes encapsuladas que não forem decodificadas diretamente pela stack Python.
- **pydicom + pylibjpeg-openjpeg**: leitura/escrita DICOM, encapsulamento por frame, e decodificação direta quando disponível.

A ideia é usar **bibliotecas maduras em C++** para a parte crítica do codec, mantendo a orquestração em Python.

## O que o script faz

- Varre recursivamente a pasta de entrada.
- Recodifica arquivos já comprimidos e também arquivos nativos (uncompressed).
- Gera saída em uma pasta `Exames-output` preservando a estrutura, **ou** faz substituição **in-place**.
- Opcionalmente cria um ZIP por paciente (`Fulano.zip`, `Ciclano.zip`, ...).
- Mantém diretórios vazios quando usado `--output-root` (por exemplo `Beltrano/` mesmo sem exames).
- Codifica em **HTJ2K Lossless** (`1.2.840.10008.1.2.4.201`).
- Para multi-frame, codifica **1 frame por codestream** e encapsula o conjunto no DICOM de saída.

## Requisitos

### macOS / Apple Silicon

```bash
brew install openjph gdcm dcmtk
```

### Python

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

Ou rode diretamente:

```bash
./install_macos_apple_silicon.sh
```

## Uso

### 1) Saída em `Exames-output` mantendo a estrutura

```bash
python compress_exames_htj2k.py ./Exames
```

Equivale a:

```bash
python compress_exames_htj2k.py ./Exames --output-root ./Exames-output
```

### 2) Saída em pasta arbitrária

```bash
python compress_exames_htj2k.py ./Exames --output-root /dados/Exames-output
```

### 3) Compressão in-place

```bash
python compress_exames_htj2k.py ./Exames --in-place
```

### 4) ZIP por paciente

```bash
python compress_exames_htj2k.py ./Exames --output-root ./Exames-output --zip-per-patient
```

### 5) In-place + ZIP por paciente

```bash
python compress_exames_htj2k.py ./Exames --in-place --zip-per-patient
```

### 6) Relatório JSON

```bash
python compress_exames_htj2k.py ./Exames --report-json ./relatorio.json
```

### 7) Ajuste fino do OpenJPH

```bash
python compress_exames_htj2k.py ./Exames \
  --num-decomps 5 \
  --block-size 64,64
```

## Observações importantes

### 1. “Lossless” aqui significa o quê?

- A saída é **HTJ2K lossless em relação aos pixels decodificados de entrada**.
- Se o arquivo original já estava em um codec **lossy**, o script **não recupera** informação perdida; ele apenas recodifica o resultado decodificado para um codestream HTJ2K lossless.
- Quando a origem já está marcada como lossy, o script preserva `LossyImageCompression = "01"`.

### 2. Fotometrias suportadas de forma conservadora

O script suporta diretamente:

- `MONOCHROME1`
- `MONOCHROME2`
- `PALETTE COLOR`
- `RGB`
- `YBR_FULL`
- `YBR_FULL_422`

No caso de `YBR_FULL_422`, o subsampling 4:2:2 é expandido durante a decodificação mínima e a saída passa a ser gravada como `YBR_FULL`.

Casos mais delicados como `YBR_RCT`, `YBR_ICT` e fotometrias exóticas foram deixados de fora por padrão para evitar mudanças implícitas de espaço de cor. O script registra falha nesses casos e segue com os demais arquivos.

### 3. Bits / tipos suportados

O script foi escrito para os casos clínicos mais comuns:

- `BitsAllocated`: 8, 16 ou 32
- `SamplesPerPixel`: 1 ou 3
- pixel data inteiro (`PixelData`), não `FloatPixelData`

### 4. ZIP de pastas já comprimidas

Como os DICOMs já estarão em HTJ2K, normalmente o ZIP adiciona pouco ganho. Por isso o padrão é `--zip-mode stored`.

Se quiser forçar ZIP com deflate:

```bash
python compress_exames_htj2k.py ./Exames --zip-per-patient --zip-mode deflated
```

## Como o script decide a decodificação

1. Primeiro tenta decodificar via `pydicom.pixels.iter_pixels(raw=True)`.
2. Se falhar e o arquivo estiver encapsulado/comprimido, usa `gdcmconv --raw --explicit` para gerar um DICOM temporário nativo.
3. Em seguida reencoda cada frame com `ojph_compress` em modo reversível (`-reversible true`).
4. Por fim, encapsula todos os codestreams gerados no DICOM final.

## Observações sobre UID / proveniência

Por padrão o script **preserva** o `SOPInstanceUID`.

Se você quiser uma política mais conservadora de proveniência, use:

```bash
python compress_exames_htj2k.py ./Exames --regenerate-sop-instance-uid
```

## Verificações úteis

Checar o Transfer Syntax UID do resultado:

```bash
dcmdump +P 0002,0010 ./Exames-output/Fulano/data-exame-1/arquivo.dcm
```

O valor esperado é:

```text
1.2.840.10008.1.2.4.201
```

## Limitações práticas

- Não implementa escrita direta via binding Python do OpenJPH; usa a CLI `ojph_compress` por frame.
- Não tenta fazer transformações automáticas de cor para fotometrias menos comuns.
- Não trata `FloatPixelData` / `DoubleFloatPixelData`.
- Em datasets muito grandes/multiframe, o processo pode exigir bastante I/O temporário, porque cada frame é passado ao `ojph_compress` como arquivo `.raw`/`.yuv` temporário.

## Estrutura do projeto

```text
htj2k-dicom-exames/
├── README.md
├── requirements.txt
├── install_macos_apple_silicon.sh
└── compress_exames_htj2k.py
```
