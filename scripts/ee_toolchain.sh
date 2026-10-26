# Shared EE compiler selection for third-party CMake dependencies.
# Source from bash after setting repo_root. Archives must come from the same
# compiler as the ELF that links them, so this follows ps2build's precedence:
# PS2BUILD_TOOLCHAIN, then ps2.yaml's toolchain:, then gcc.
#
# Sets: ps2build_root toolchain cc ar ranlib target_flags
# Provides: native_path, cmake_toolchain_args

ps2build_root="${PS2BUILD_ROOT:-$HOME/.local/share/ps2build}"

toolchain="${PS2BUILD_TOOLCHAIN:-}"
if [[ -z "${toolchain}" ]]; then
    toolchain="$(sed -n 's/^toolchain:[[:space:]]*\([a-z]*\).*/\1/p' "${repo_root}/ps2.yaml" | head -n 1)"
fi
toolchain="${toolchain:-gcc}"

native_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

host_tool() {
    if [[ -x "$1" ]]; then printf '%s\n' "$1"; elif [[ -x "$1.exe" ]]; then printf '%s\n' "$1.exe"; else return 1; fi
}

case "${toolchain}" in
gcc)
    prefix="${ps2build_root}/toolchain/ee/bin/mips64r5900el-ps2-elf"
    cc="$(host_tool "${prefix}-gcc")" || cc=""
    ar="$(host_tool "${prefix}-ar")" || ar=""
    ranlib="$(host_tool "${prefix}-ranlib")" || ranlib=""
    target_flags=""
    ;;
llvm)
    llvm_root="${ps2build_root}/llvm-toolchain"
    cc="$(host_tool "${llvm_root}/llvm/bin/clang")" || cc=""
    ar="$(host_tool "${llvm_root}/llvm/bin/llvm-ar")" || ar=""
    ranlib="$(host_tool "${llvm_root}/llvm/bin/llvm-ranlib")" || ranlib=""
    cfg="${llvm_root}/ee/bin/mips64r5900el-ps2-elf-clang.cfg"
    if [[ ! -f "${cfg}" ]]; then
        echo "PS2 EE clang config not found at ${cfg}" >&2
        exit 1
    fi
    # The SDK's EE clang driver is clang plus this config file.
    target_flags="--config=$(native_path "${cfg}") "
    ;;
*)
    echo "Unsupported ps2build toolchain: ${toolchain}" >&2
    exit 1
    ;;
esac

if [[ -z "${cc}" || -z "${ar}" || -z "${ranlib}" ]]; then
    echo "PS2 EE ${toolchain} compiler tools not found under ${ps2build_root}" >&2
    echo "Install/update ps2build first, or set PS2BUILD_ROOT." >&2
    exit 1
fi
echo "Using the ${toolchain} EE toolchain: ${cc}"

# Common CMake arguments for a static, freestanding EE library build.
cmake_toolchain_args() {
    printf '%s\n' \
        -DCMAKE_SYSTEM_NAME=Generic \
        -DCMAKE_SYSTEM_PROCESSOR=mips \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        "-DCMAKE_C_COMPILER=$(native_path "${cc}")" \
        "-DCMAKE_AR=$(native_path "${ar}")" \
        "-DCMAKE_RANLIB=$(native_path "${ranlib}")" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=MinSizeRel
}
