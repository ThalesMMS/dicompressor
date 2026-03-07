#!/usr/bin/env bash
set -euo pipefail

"$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/bootstrap_deps_macos_arm64.sh"

echo
echo "C++ dependencies installed in .deps/install/macos-arm64."
echo "Configure with:"
echo "  cmake --preset macos-arm64-release -DCMAKE_PREFIX_PATH=$PWD/.deps/install/macos-arm64"
