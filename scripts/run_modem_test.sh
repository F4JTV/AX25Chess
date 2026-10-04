#!/usr/bin/env bash
#
# End to end over the embedded modem: CHS-1 frames made into audio by Dire
# Wolf's gen_packets, streamed to tst_modem's standard input, decoded by the
# core and handed to a game session.  Run by ctest when gen_packets is found.
#
#   scripts/run_modem_test.sh <build dir> [gen_packets]
#
# This file is part of AX25Chess.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

BUILD="${1:-build}"
GEN="${2:-$(command -v gen_packets || true)}"
[ -n "$GEN" ] || { echo "gen_packets (from a Dire Wolf build) is not on PATH" >&2; exit 2; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The frames: written by tst_modem itself, so they carry valid CRCs.
"$BUILD/tst_modem" --print-frames > "$TMP/msgs.txt"
"$GEN" -o "$TMP/test.wav" -r 44100 "$TMP/msgs.txt" >/dev/null 2>&1

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
(
    cat "$TMP/test.wav"
    # Silence afterwards, at the pace of real audio, so the input stays open
    # while the test looks at what was decoded.
    python3 - <<'PY' 2>/dev/null || true
import sys, time
chunk = b"\x00" * 8820          # 0.1 s of 44.1 kHz 16-bit mono
for _ in range(200):
    sys.stdout.buffer.write(chunk)
    sys.stdout.buffer.flush()
    time.sleep(0.1)
PY
) | "$BUILD/tst_modem" --conf "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/conf/test-stdin.conf"
