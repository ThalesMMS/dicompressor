# PERFORMANCE_NOTES

## Eliminated bottlenecks

- No `subprocess` in hot path.
- No temporary `.raw`, `.yuv` or `.j2c`.
- HTJ2K encode directly in memory with `ojph::mem_outfile`.
- Parallelism per file with fixed thread pool.
- Reuse of `ojph::codestream` with `restart()`.

## Parallelism

- Dynamic scheduler in `core/job_scheduler.cpp`.
- `--workers` controls the number of workers.
- No massive `std::async` and no spawn per frame.

## Memory

- Frame-by-frame decoding.
- Does not materialize all decoded frames of a study at the same time.
- Multi-frame encapsulation appends each encoded codestream incrementally into the output `DcmPixelSequence` instead of retaining a separate `std::vector` of all encoded frames.
- DCMTK still owns the compressed frame data inside the output pixel sequence, so this removes redundant double-buffering during encode/encapsulation rather than making large multi-frame writes true constant-memory streaming.
- Reused scratch buffers.

## I/O

- Discovery with fast-path for `.dcm`.
- Safe writing with neighbor temp file + `fsync` + `rename`.
- In `--in-place`, the original only leaves after successful writing.

## Emitted metrics

- `files/s`
- `frames/s`
- `MPix/s`
- `bytes_read`
- `bytes_written`
- breakdown per phase in JSON report

## Next steps

- Persistent `WorkerContext` per thread.
- RSS metrics and p50/p95 histograms.
- Vectorized fast-paths for mono 16-bit and RGB 8-bit.
