#!/usr/bin/env bash
#
# Install everything needed to build and debug gpui on Ubuntu/Debian.
# Non-interactive: safe to run from a script, a Dockerfile, or `wsl bash`.
#
#   bash cmd/ubuntu-install-deps.sh
#   bash cmd/ubuntu-install-deps.sh --build-only   # just what a compile needs
#   bash cmd/ubuntu-install-deps.sh --without-webkit  # leave WebKitGTK out
#
# Installs:
#   build-essential, clang, lld, pkg-config  — the C++ toolchain
#   gdb, lldb, valgrind                      — debuggers
#   libx11-dev, libxext-dev                  — the X11 window
#   libcairo2-dev, libpango1.0-dev           — the 2D backend (Paint_linux.cpp)
#   libgdk-pixbuf-2.0-dev                    — JPEG/GIF/WebP decode (Paint_linux.cpp)
#   libcurl4-openssl-dev                     — the HTTP client (sys/http_linux.cpp)
#   libasound2-dev                           — the microphone (sys/audio_input_linux.cpp)
#   libwebkit2gtk-4.1-dev                    — the webview (wry/wry_linux.cpp)
#   xvfb, xauth                              — a display for headless UI tests
#   fonts-dejavu-core, fonts-noto-cjk        — the Sans / Monospace families
#   git, curl, unzip                         — fetching the Rust spec tree
#   bun                                      — runs cmd/*.ts
#   rustup + stable toolchain                — for `-compare` runs
#
# Rust and bun are per-user installs; everything else goes through apt.
#
# --build-only stops after the apt packages a compile needs: no debuggers, no
# CJK fonts, no bun, no rustup. That is what CI runs, so the compile
# dependencies have one source of truth.
#
# libcurl, WebKitGTK and ALSA are soft dependencies: cmd/build.ts compiles without
# either. --without-webkit leaves WebKitGTK (and the GTK 3 it pulls in) out,
# which is how CI keeps the build without a webview compiling too.

set -euo pipefail

BUILD_ONLY=0
WEBKIT=1
for arg in "$@"; do
  case "$arg" in
  --build-only | -build-only) BUILD_ONLY=1 ;;
  --without-webkit | -without-webkit) WEBKIT=0 ;;
  *)
    echo "unknown option: $arg" >&2
    echo "usage: bash cmd/ubuntu-install-deps.sh [--build-only] [--without-webkit]" >&2
    exit 1
    ;;
  esac
done

export DEBIAN_FRONTEND=noninteractive

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "Run this as root, or install sudo first." >&2
    exit 1
  fi
fi

APT_FLAGS="-y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold"

echo "==> apt-get update"
$SUDO apt-get update $APT_FLAGS

echo "==> apt-get install"
$SUDO apt-get install $APT_FLAGS --no-install-recommends \
  build-essential \
  clang \
  lld \
  pkg-config \
  libx11-dev \
  libxext-dev \
  libcairo2-dev \
  libpango1.0-dev \
  libgdk-pixbuf-2.0-dev \
  libglib2.0-dev \
  libcurl4-openssl-dev \
  libasound2-dev \
  xvfb \
  xauth \
  fonts-dejavu-core \
  fontconfig \
  ca-certificates \
  curl \
  git \
  unzip \
  xz-utils

if [ "$WEBKIT" = "1" ]; then
  echo "==> WebKitGTK (the webview)"
  $SUDO apt-get install $APT_FLAGS --no-install-recommends libwebkit2gtk-4.1-dev
fi

if [ "$BUILD_ONLY" = "1" ]; then
  echo ""
  echo "==> versions"
  "${CXX:-g++}" --version | head -1 || true
  pkg-config --modversion x11 cairo pangocairo | tr '\n' ' ' && echo "(x11 cairo pangocairo)"
  echo ""
  echo "Done (--build-only). Compile with: bun cmd/build.ts -rel -all"
  exit 0
fi

echo "==> debuggers"
# lldb is not in every Ubuntu release's main pocket; it is installed on its
# own below so a missing package does not fail the whole run.
$SUDO apt-get install $APT_FLAGS --no-install-recommends gdb valgrind

echo "==> CJK fonts (optional)"
# The story gallery has a Chinese link; without these it renders as tofu.
$SUDO apt-get install $APT_FLAGS --no-install-recommends fonts-noto-cjk ||
  echo "fonts-noto-cjk is unavailable; CJK text will fall back to boxes."

echo "==> lldb (optional)"
$SUDO apt-get install $APT_FLAGS --no-install-recommends lldb ||
  echo "lldb is unavailable on this release; gdb is installed and is what cmd/run.ts -gdb uses."

if command -v bun >/dev/null 2>&1; then
  echo "==> bun already installed ($(bun --version))"
else
  echo "==> installing bun"
  curl -fsSL https://bun.sh/install | bash
  # The installer appends to ~/.bashrc; make it visible to the rest of this
  # script too.
  export BUN_INSTALL="${BUN_INSTALL:-$HOME/.bun}"
  export PATH="$BUN_INSTALL/bin:$PATH"
fi

if command -v cargo >/dev/null 2>&1; then
  echo "==> rust already installed ($(rustc --version 2>/dev/null || echo unknown))"
else
  echo "==> installing rust (rustup, stable)"
  curl -fsSL https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain stable
  export PATH="$HOME/.cargo/bin:$PATH"
fi

echo ""
echo "==> versions"
"${CXX:-g++}" --version | head -1 || true
clang++ --version | head -1 || true
gdb --version | head -1 || true
pkg-config --modversion x11 cairo pangocairo | tr '\n' ' ' && echo "(x11 cairo pangocairo)"
command -v bun >/dev/null 2>&1 && echo "bun $(bun --version)"
command -v cargo >/dev/null 2>&1 && echo "$(cargo --version)"

echo ""
echo "Done. Open a new shell (or 'source ~/.bashrc') so bun and cargo are on PATH, then:"
echo "  bun cmd/build.ts -rel hello_world"
echo "  bun cmd/run.ts -rel system_monitor"
