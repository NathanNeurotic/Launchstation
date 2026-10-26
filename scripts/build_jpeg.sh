#!/usr/bin/env bash
set -euo pipefail

# Build libjpeg-turbo (IJG/BSD-3-Clause/zlib) for the active EE toolchain.
# Output is intentionally untracked under .jpeg-deps/.
#
# PS2BUILD's LLVM package tier currently ships the GCC-built world/jpeg
# archive unchanged (identical bytes, GCC .comment), which ld.lld rejects
# ("symbol has invalid binding"). Build the same upstream release and
# configuration as the SDK package (3.1.3, 8-bit, arithmetic coding, memory
# source/destination, no SIMD) with the selected compiler instead.
# The archive is installed as libjpegturbo.a so -ljpegturbo can never resolve
# to the SDK package's libjpeg.a.
version="3.1.3"
archive="libjpeg-turbo-${version}.tar.gz"
sha256="075920b826834ac4ddf97661cc73491047855859affd671d52079c6867c1c6c0"
url="https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${version}/${archive}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${repo_root}/.jpeg-deps"
cache="${repo_root}/.cache/jpeg-deps"
# Selects the EE compiler (gcc/llvm) the same way ps2build does.
source "${repo_root}/scripts/ee_toolchain.sh"
mkdir -p "${cache}"

if [[ ! -f "${cache}/${archive}" ]]; then
    curl -fL --retry 6 --retry-all-errors "${url}" -o "${cache}/${archive}"
fi
echo "${sha256}  ${cache}/${archive}" | sha256sum -c -

rm -rf "${out}"
mkdir -p "${out}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
tar -xzf "${cache}/${archive}" -C "${work}"
src="${work}/libjpeg-turbo-${version}"
build="${work}/build"

mapfile -t common < <(cmake_toolchain_args)
cmake -S "${src}" -B "${build}" "${common[@]}" \
    -DCMAKE_INSTALL_PREFIX="$(native_path "${out}")" \
    -DENABLE_SHARED=OFF \
    -DENABLE_STATIC=ON \
    -DWITH_SIMD=OFF \
    -DWITH_TURBOJPEG=OFF \
    -DWITH_TOOLS=OFF \
    -DWITH_TESTS=OFF \
    -DWITH_ARITH_ENC=ON \
    -DWITH_ARITH_DEC=ON \
    -DCMAKE_C_FLAGS="${target_flags}-G0 -ffunction-sections -fdata-sections"

cmake --build "${build}" --parallel
cmake --install "${build}"

lib_dir="${out}/lib"
[[ -f "${lib_dir}/libjpeg.a" ]] || lib_dir="${out}/lib64"
mv "${lib_dir}/libjpeg.a" "${out}/libjpegturbo.a"
rm -rf "${out}/lib" "${out}/lib64" "${out}/bin" "${out}/share"
mkdir -p "${out}/lib"
mv "${out}/libjpegturbo.a" "${out}/lib/libjpegturbo.a"

test -f "${out}/include/jpeglib.h"
grep -q "LIBJPEG_TURBO_VERSION  ${version}" "${out}/include/jconfig.h"
test -f "${out}/lib/libjpegturbo.a"
printf '%s\n' "${toolchain}" > "${out}/TOOLCHAIN"

echo "libjpeg-turbo ${version} ready in ${out}"
