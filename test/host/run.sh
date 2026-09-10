#!/usr/bin/env bash
# Host unit tests for the pure C modules: the letter scrubber (scrub.c) and
# the persist layer (store.c, against an in-memory persist). Built with the
# system compiler; test/host/pebble.h stands in for the SDK header.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
OUT="${TMPDIR:-/tmp}/pebble-crossword-host"
mkdir -p "$OUT"
CC="${CC:-cc}"
$CC -std=c11 -Wall -Wextra -Werror -O1 -I src/c src/c/scrub.c test/host/test_scrub.c -o "$OUT/test_scrub"
$CC -std=c11 -Wall -Werror -O1 -I test/host -I src/c src/c/store.c test/host/test_store.c -o "$OUT/test_store"
"$OUT/test_scrub"
"$OUT/test_store"
