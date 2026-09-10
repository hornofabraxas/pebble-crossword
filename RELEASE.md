# Releasing

1. Bump `version` in `package.json` (`major.minor`).
2. Add a `## vX.Y` section to `CHANGELOG.md`.
3. Commit both.
4. `bash scripts/release.sh` — verifies a clean build, tags `vX.Y`, and pushes the
   tag. The `release` workflow builds and attaches `pebble-crossword-vX.Y.pbw` to a
   GitHub Release.

The pre-push hook (`bash scripts/setup-hooks.sh`, once per clone) runs the pkjs
tests and a build before anything reaches GitHub. CI re-runs the same gate.
