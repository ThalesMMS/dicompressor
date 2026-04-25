# Contributing

Thanks for helping improve `dicompressor`. This project is a small C++/CMake CLI, so contributions work best when they are focused, easy to review, and backed by a clear reproduction or validation step.

## Getting Started

Use the build and test workflow documented in [README.md](./README.md):

- bootstrap dependencies with the scripts in [`scripts/`](./scripts)
- configure with the matching CMake preset for your platform
- build the CLI with the documented `cmake --build` command
- run the test suite before opening a pull request

For sanitizer coverage, prefer:

```bash
ctest --preset debug-sanitized
```

If that preset is not available on your platform, run the closest documented test command from the README and mention it in your pull request.

## Reporting Bugs

Please do not attach protected health information or identifiable medical-imaging data to public issues. If a sample is needed, use a de-identified file or describe the file characteristics instead.

Useful DICOM/transcoding bug reports include:

- platform and compiler details
- source Transfer Syntax UID
- command line used
- expected result and actual result
- whether the input is single-frame or multi-frame
- dimensions, samples per pixel, bits allocated/stored, and photometric interpretation when relevant
- whether `--strict-color`, `--in-place`, `--output-root`, ZIP output, or JSON reporting was involved

If the CLI produced logs or a JSON report, include the relevant excerpt.
Redact or sanitize PHI and secrets before including any logs or reports; use `Resources/SampleData/SampleDICOM/` as the canonical demo data source instead of real patient data.

## Pull Requests

Keep changes surgical and match the existing style. Avoid speculative refactors, broad formatting changes, or new abstractions unless they are needed for the fix or feature.

Before submitting, run `ctest --preset debug-sanitized` when practical. In the pull request, include what changed, how you validated it, and whether any datasets, fixtures, or generated reports were touched.

The coding expectations in [AGENTS.md](./AGENTS.md) are a good shorthand for this repository: think before coding, prefer simple changes, touch only what is needed, and verify the stated goal.
