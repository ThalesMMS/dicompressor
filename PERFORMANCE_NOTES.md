# PERFORMANCE_NOTES

## Gargalos eliminados

- Sem `subprocess` no hot path.
- Sem `.raw`, `.yuv` ou `.j2c` temporários.
- Encode HTJ2K direto em memória com `ojph::mem_outfile`.
- Paralelismo por arquivo com thread pool fixo.
- Reuso do `ojph::codestream` com `restart()`.

## Paralelismo

- Scheduler dinâmico em `core/job_scheduler.cpp`.
- `--workers` controla o número de workers.
- Sem `std::async` massivo e sem spawn por frame.

## Memória

- Decodificação frame a frame.
- Não materializa todos os frames decodificados de um estudo ao mesmo tempo.
- Buffers de scratch reaproveitados.

## I/O

- Descoberta com fast-path para `.dcm`.
- Escrita segura com temp file vizinho + `fsync` + `rename`.
- Em `--in-place`, o original só sai depois da escrita bem-sucedida.

## Métricas emitidas

- `files/s`
- `frames/s`
- `MPix/s`
- `bytes_read`
- `bytes_written`
- breakdown por fase no report JSON

## Próximos passos

- `WorkerContext` persistente por thread.
- Encapsulamento streaming para multi-frame muito grande.
- Métricas de RSS e histogramas p50/p95.
- Fast-paths vetorizados para mono 16-bit e RGB 8-bit.
