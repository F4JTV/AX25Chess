#!/usr/bin/env bash
# The Windows files must carry CRLF endings: cmd.exe misparses a batch file
# with bare line feeds ("X was unexpected at this time").  Fails when one
# of them has none; repairs with --fix.
set -u
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
status=0
for f in "$SRC_DIR"/build_all.bat "$SRC_DIR"/installer/*.iss "$SRC_DIR"/assets/*.rc "$SRC_DIR"/scripts/*.ps1; do
    [ -f "$f" ] || continue
    crlf=$(awk 'BEGIN{n=0} /\r$/ {n++} END{print n}' "$f")
    if [ "$crlf" -eq 0 ]; then
        if [ "${1:-}" = "--fix" ]; then
            sed -i 's/\r$//; s/$/\r/' "$f"
            echo "fixed: $f"
        else
            echo "no CRLF: $f"
            status=1
        fi
    fi
done
[ $status -eq 0 ] && echo "Windows line endings in order."
exit $status
