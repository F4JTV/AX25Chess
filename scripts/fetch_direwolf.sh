#!/usr/bin/env bash
#
# Fetch the Dire Wolf sources the embedded core is built from, and apply the
# patches in patches/direwolf.  The tree lands in external/direwolf (or in
# $DIREWOLF_DIR).  Safe to run again: an existing tree is kept and patches
# already applied are skipped.
#
#   DIREWOLF_REF   git tag or branch to check out, default 1.8
#   DIREWOLF_REPO  repository, default https://github.com/wb2osz/direwolf.git
#   DIREWOLF_DIR   destination, default external/direwolf
#
# Needs git and patch.  On Windows, run it from Git Bash or MSYS2.
#
# This file is part of AX25Chess.
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${DIREWOLF_DIR:-$ROOT/external/direwolf}"
REF="${DIREWOLF_REF:-1.8}"
REPO="${DIREWOLF_REPO:-https://github.com/wb2osz/direwolf.git}"

if [ -e "$DEST/src/direwolf.h" ]; then
    echo "Dire Wolf sources already present in $DEST"
else
    # The folder exists in the archive with a README in it, and git refuses
    # to clone into a folder that is not empty: anything there that is not
    # a Dire Wolf tree goes.
    if [ -d "$DEST" ]; then
        echo "Removing $DEST (no Dire Wolf sources in it)"
        rm -rf "$DEST"
    fi
    echo "Cloning $REPO ($REF) into $DEST"
    git clone --depth 1 --branch "$REF" "$REPO" "$DEST"
fi

cd "$DEST"
for p in "$ROOT"/patches/direwolf/*.patch; do
    name="$(basename "$p")"
    if patch -p1 -N -s --dry-run < "$p" >/dev/null 2>&1; then
        echo "Applying $name"
        patch -p1 -N -s < "$p"
    elif patch -p1 -R -s --dry-run < "$p" >/dev/null 2>&1; then
        echo "Already applied: $name"
    else
        echo "ERROR: $name does not apply to the sources in $DEST" >&2
        echo "       (expected Dire Wolf $REF; set DIREWOLF_REF if you meant another version)" >&2
        exit 1
    fi
done

if ! grep -q tq_term src/tq.h; then
    echo "ERROR: patch marker missing after applying patches" >&2
    exit 1
fi

version="$(sed -n 's/^set(direwolf_VERSION_MAJOR "\([0-9]*\)").*/\1/p' CMakeLists.txt).$(sed -n 's/^set(direwolf_VERSION_MINOR "\([0-9]*\)").*/\1/p' CMakeLists.txt)"
echo "Dire Wolf $version ready in $DEST"
