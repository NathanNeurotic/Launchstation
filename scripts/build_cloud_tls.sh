#!/usr/bin/env bash
set -euo pipefail

# Build the Apache-2.0 Mbed TLS dependency for the PS2 EE toolchain.
# Output is intentionally untracked under .cloud-deps/.
version="3.6.7"
archive="mbedtls-${version}.tar.bz2"
sha256="a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
url="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${version}/${archive}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${repo_root}/.cloud-deps"
cache="${repo_root}/.cache/cloud-deps"
ps2build_root="${PS2BUILD_ROOT:-$HOME/.local/share/ps2build}"
toolbin="${ps2build_root}/toolchain/ee/bin"
prefix="${toolbin}/mips64r5900el-ps2-elf"

mkdir -p "${cache}"
if [[ ! -x "${prefix}-gcc" ]]; then
    echo "PS2 EE compiler not found at ${prefix}-gcc" >&2
    echo "Install/update ps2build first, or set PS2BUILD_ROOT." >&2
    exit 1
fi

if [[ ! -f "${cache}/${archive}" ]]; then
    curl -fL --retry 6 --retry-all-errors "${url}" -o "${cache}/${archive}"
fi
echo "${sha256}  ${cache}/${archive}" | sha256sum -c -

rm -rf "${out}"
mkdir -p "${out}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
tar -xjf "${cache}/${archive}" -C "${work}"
src="${work}/mbedtls-${version}"
build="${work}/build"

# The EE target is neither POSIX nor Windows. Keep certificate/date support,
# but provide the millisecond clock from wLaunchELF and omit Mbed TLS's
# Unix/Windows timing module.
python3 "${src}/scripts/config.py" -f "${src}/include/mbedtls/mbedtls_config.h" unset MBEDTLS_TIMING_C
python3 "${src}/scripts/config.py" -f "${src}/include/mbedtls/mbedtls_config.h" unset MBEDTLS_NET_C
python3 "${src}/scripts/config.py" -f "${src}/include/mbedtls/mbedtls_config.h" set MBEDTLS_PLATFORM_MS_TIME_ALT

cmake -S "${src}" -B "${build}"     -DCMAKE_SYSTEM_NAME=Generic     -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY     -DCMAKE_C_COMPILER="${prefix}-gcc"     -DCMAKE_AR="${prefix}-ar"     -DCMAKE_RANLIB="${prefix}-ranlib"     -DCMAKE_INSTALL_PREFIX="${out}"     -DENABLE_PROGRAMS=OFF     -DENABLE_TESTING=OFF     -DBUILD_SHARED_LIBS=OFF     -DMBEDTLS_FATAL_WARNINGS=OFF     -DCMAKE_BUILD_TYPE=MinSizeRel     -DCMAKE_C_FLAGS="-G0 -Os -ffunction-sections -fdata-sections -DMBEDTLS_NO_PLATFORM_ENTROPY -DMBEDTLS_ENTROPY_HARDWARE_ALT"

cmake --build "${build}" --parallel
cmake --install "${build}"

test -f "${out}/include/mbedtls/ssl.h"
test -f "${out}/lib/libmbedtls.a"
test -f "${out}/lib/libmbedx509.a"
test -f "${out}/lib/libmbedcrypto.a"

echo "Native cloud TLS dependency ready in ${out}"
