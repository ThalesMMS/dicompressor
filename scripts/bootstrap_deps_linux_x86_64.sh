#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/.deps"
SRC_DIR="${DEPS_DIR}/src"
BUILD_DIR="${DEPS_DIR}/build"
INSTALL_DIR="${DEPS_DIR}/install/linux-x86_64"
JOBS="$(nproc 2>/dev/null || echo 8)"

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}"

fetch() {
  local url="$1"
  local output="$2"
  if [[ ! -f "${output}" ]]; then
    curl -L "${url}" -o "${output}"
  fi
}

extract() {
  local archive="$1"
  local dest="$2"
  rm -rf "${dest}"
  mkdir -p "${dest}"
  tar -xf "${archive}" -C "${dest}" --strip-components=1
}

build_cmake_project() {
  local source_dir="$1"
  local build_dir="$2"
  shift 2
  cmake -S "${source_dir}" -B "${build_dir}" -G Ninja "$@"
  cmake --build "${build_dir}" -j "${JOBS}"
  cmake --install "${build_dir}"
}

fetch "https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.4.tar.gz" "${SRC_DIR}/openjpeg-2.5.4.tar.gz"
fetch "https://github.com/aous72/OpenJPH/archive/refs/tags/0.26.3.tar.gz" "${SRC_DIR}/openjph-0.26.3.tar.gz"
fetch "https://github.com/DCMTK/dcmtk/archive/refs/tags/DCMTK-3.7.0.tar.gz" "${SRC_DIR}/dcmtk-3.7.0.tar.gz"

extract "${SRC_DIR}/openjpeg-2.5.4.tar.gz" "${SRC_DIR}/openjpeg-2.5.4"
extract "${SRC_DIR}/openjph-0.26.3.tar.gz" "${SRC_DIR}/openjph-0.26.3"
extract "${SRC_DIR}/dcmtk-3.7.0.tar.gz" "${SRC_DIR}/dcmtk-3.7.0"

build_cmake_project \
  "${SRC_DIR}/openjpeg-2.5.4" \
  "${BUILD_DIR}/openjpeg-2.5.4" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DBUILD_CODEC=OFF \
  -DBUILD_JPIP=OFF \
  -DBUILD_JPWL=OFF \
  -DBUILD_DOC=OFF \
  -DBUILD_TESTING=OFF

build_cmake_project \
  "${SRC_DIR}/openjph-0.26.3" \
  "${BUILD_DIR}/openjph-0.26.3" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DBUILD_TESTING=OFF \
  -DOJPH_DISABLE_SIMD=OFF

build_cmake_project \
  "${SRC_DIR}/dcmtk-3.7.0" \
  "${BUILD_DIR}/dcmtk-3.7.0" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DDCMTK_WITH_OPENJPEG=ON \
  -DDCMTK_WITH_SNDFILE=OFF \
  -DDCMTK_WITH_XML=ON \
  -DDCMTK_WITH_ZLIB=ON \
  -DDCMTK_BUILD_APPS=ON \
  -DDCMTK_BUILD_TESTS=OFF \
  -DDCMTK_BUILD_DOCS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_PREFIX_PATH="${INSTALL_DIR}"

cat <<EOF

Bootstrap completed.
Dependencies installed in:
  ${INSTALL_DIR}

Configure the project with:
  cmake --preset release -DCMAKE_PREFIX_PATH=${INSTALL_DIR}
EOF
