#!/usr/bin/env bash
# check-appimage-size.sh — fail the build if the watch app image exceeds the Pebble ceiling.
#
# Pebble caps a single app image's VIRTUAL_SIZE (static .text + .data + .bss, i.e. the
# ELF's LOAD segment MemSiz) at 65,535 bytes. Fonts/images do NOT count. Going over means
# the watch silently refuses to load the app. Crossword sits at about half of it; this
# keeps an eye on the headroom as features land.
#
# Usage: check-appimage-size.sh [path/to/pebble-app.elf]
#   defaults to build/emery/pebble-app.elf
set -euo pipefail

CEILING=65535
WARN_HEADROOM=2048
ELF="${1:-build/emery/pebble-app.elf}"

if [[ ! -f "$ELF" ]]; then
  echo "check-appimage-size: ELF not found: $ELF" >&2
  exit 2
fi

# Any readelf can read the ELF's program headers cross-arch. Prefer the arm one,
# then a generic readelf (present on CI runners), then a local macOS Pebble SDK.
READELF="$(command -v arm-none-eabi-readelf || command -v readelf || true)"
if [[ -z "$READELF" ]]; then
  for cand in "$HOME/Library/Application Support/Pebble SDK/SDKs"/*/toolchain/arm-none-eabi/bin/arm-none-eabi-readelf; do
    [[ -x "$cand" ]] && READELF="$cand" && break
  done
fi
if [[ -z "$READELF" ]]; then
  echo "check-appimage-size: no arm-none-eabi-readelf on PATH or in ~/Library Pebble SDK" >&2
  exit 2
fi

# First LOAD program header; column 6 is MemSiz (hex, e.g. 0x0fe41).
MEMSIZE_HEX="$("$READELF" -l "$ELF" | awk '/LOAD/ {print $6; exit}')"
if [[ -z "$MEMSIZE_HEX" ]]; then
  echo "check-appimage-size: could not read LOAD MemSiz from $ELF" >&2
  exit 2
fi
MEMSIZE=$(( MEMSIZE_HEX ))   # bash parses 0x.. hex directly
HEADROOM=$(( CEILING - MEMSIZE ))

printf 'App image: %d bytes  (ceiling %d, headroom %d)\n' "$MEMSIZE" "$CEILING" "$HEADROOM"

if (( MEMSIZE >= CEILING )); then
  echo "FAIL: at/over the ${CEILING}-byte app-image ceiling — the watch will not load." >&2
  exit 1
fi
if (( HEADROOM < WARN_HEADROOM )); then
  echo "::warning::app-image headroom is only ${HEADROOM} bytes (under ${WARN_HEADROOM})."
fi
echo "OK: under the app-image ceiling."
