#!/bin/sh
# Build the co-op module and package it into dist/. Uses devkitA64 + libnx when DEVKITPRO is set, the
# devkitpro/devkita64 Docker image otherwise. Make cannot handle spaces, so keep this folder on a space-free path.
set -e
P="$(cd "$(dirname "$0")" && pwd)"
if [ -n "$DEVKITPRO" ]; then
    make -C "$P" -j"$(nproc)"
else
    W="$(cygpath -w "$P" 2>/dev/null || echo "$P")"
    MSYS_NO_PATHCONV=1 docker run --rm -v "$W:/work" -w /work devkitpro/devkita64 make -j8
fi
D="$P/dist/Origins Coop/exefs"
rm -rf "$P/dist"; mkdir -p "$D"
# No main.npdm: the game's own one is used, which keeps this mod from fighting the Dynamic FPS mod over that file.
cp "$P/deploy/subsdk8" "$D/"
cp "$P/README.md" "$P/CONTROLS.html" "$P/dist/Origins Coop/"
ls -l "$D"
