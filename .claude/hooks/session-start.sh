#!/bin/bash
# Prepare a Claude Code cloud session for Launchstation: ps2build SDK + LLVM/Clang
# toolchain (the same one CI uses, see .github/workflows/compile.yml).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

SDK="${PS2DEV_INSTALL_DIR:-$HOME/.local/share/ps2build}"
cd "${CLAUDE_PROJECT_DIR:-$(pwd)}"

# The SDK CI pins in .github/ps2build-sdk.env, LLVM included, installed by the same script CI uses. An SDK that
# already holds those versions is left alone, and no session asks the official releases which release is newest.
# The official host resets HTTP/2 streams under load; ps2build is a Go binary.
GODEBUG=http2client=0 sh scripts/ps2build_sdk.sh --llvm "$SDK"
export PATH="$SDK:$PATH"

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  {
    echo "export PATH=\"$SDK:\$PATH\""
    echo "export LLVM_BIN=\"$SDK/llvm-toolchain/llvm/bin\""
    echo "export PS2BUILD_TOOLCHAIN=llvm"
  } >> "$CLAUDE_ENV_FILE"
fi

ps2build version
