#!/bin/bash
# Prepare a Claude Code cloud session for Launchstation: ps2build SDK + LLVM/Clang
# toolchain (the same one CI uses, see .github/workflows/compile.yml).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

SDK="${PS2DEV_INSTALL_DIR:-$HOME/.local/share/ps2build}"
cd "${CLAUDE_PROJECT_DIR:-$(pwd)}"

if [ ! -x "$SDK/ps2build" ]; then
  installer="$(mktemp)"
  trap 'rm -f "$installer"' EXIT
  curl -sSfL --retry 8 --retry-delay 5 --retry-all-errors --connect-timeout 20 \
    https://git.techwritescode.dev/ps2/install/raw/branch/master/install.sh \
    -o "$installer"
  sh "$installer"
fi
export PATH="$SDK:$PATH"

# Idempotent: refreshes the SDK package set, then ensures the LLVM toolchain.
ps2build update
if [ ! -f "$SDK/llvm-toolchain/ee/bin/mips64r5900el-ps2-elf-clang.cfg" ]; then
  ps2build update llvm-toolchain
fi

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  {
    echo "export PATH=\"$SDK:\$PATH\""
    echo "export LLVM_BIN=\"$SDK/llvm-toolchain/llvm/bin\""
    echo "export PS2BUILD_TOOLCHAIN=llvm"
  } >> "$CLAUDE_ENV_FILE"
fi

ps2build version
