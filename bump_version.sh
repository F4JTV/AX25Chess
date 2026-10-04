#!/usr/bin/env bash
# ============================================================================
#  AX25Chess - bump the version everywhere at once
#
#  Every fix and every feature gets a new version. This script updates
#  CMakeLists.txt, CHANGELOG.md and the Debian changelog together, so the
#  three never disagree. The AppStream metadata takes its version from
#  CMake at build time and needs nothing here.
#
#  Usage:
#    ./bump_version.sh 1.0.3 "Fix the GPS port scan on Windows" ["second line"...]
#
#  Each extra argument becomes one bullet in both changelogs.
# ============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NEW="${1:-}"
shift || true

[ -n "$NEW" ] || { echo "usage: $0 x.y.z \"change\" [\"change\"...]" >&2; exit 2; }
printf '%s' "$NEW" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "Version must be x.y.z" >&2; exit 2; }
[ $# -gt 0 ] || { echo "Give at least one change line" >&2; exit 2; }

OLD="$(sed -n 's/^project(AX25Chess VERSION \([0-9.]*\).*/\1/p' "$SRC_DIR/CMakeLists.txt" | head -1)"
[ -n "$OLD" ] || { echo "Could not read the current version from CMakeLists.txt" >&2; exit 1; }
[ "$OLD" != "$NEW" ] || { echo "Already at $NEW" >&2; exit 1; }

DATE_ISO="$(date +%Y-%m-%d)"
DATE_RFC="$(date -R)"

# CMakeLists.txt
sed -i "s/^project(AX25Chess VERSION $OLD /project(AX25Chess VERSION $NEW /" "$SRC_DIR/CMakeLists.txt"

# CHANGELOG.md: a new section after the header paragraph.
SECTION="$(printf '## %s - %s\n\n%s\n' "$NEW" "$DATE_ISO" "$(printf -- '- %s\n' "$@")")
"
export SECTION
awk 'BEGIN {done=0} /^## / && !done {print ENVIRON["SECTION"]; done=1} {print}' \
    "$SRC_DIR/CHANGELOG.md" > "$SRC_DIR/CHANGELOG.md.new" \
    && mv "$SRC_DIR/CHANGELOG.md.new" "$SRC_DIR/CHANGELOG.md"

# Debian changelog: a new entry on top.
{
    printf 'ax25chess (%s) unstable; urgency=medium\n\n' "$NEW"
    # Debian policy wants lines under 80 columns; wrap each change.
    for change in "$@"; do
        printf '%s\n' "$change" | fold -s -w 74 | sed -e '1s/^/  * /' -e '2,$s/^/    /' -e 's/ *$//'
    done
    printf '\n -- AX25Chess Community <noreply@ax25chess.invalid>  %s\n\n' "$DATE_RFC"
    cat "$SRC_DIR/packaging/deb/changelog"
} > "$SRC_DIR/packaging/deb/changelog.new" && mv "$SRC_DIR/packaging/deb/changelog.new" "$SRC_DIR/packaging/deb/changelog"

echo "$OLD -> $NEW: CMakeLists.txt, CHANGELOG.md, packaging/deb/changelog updated."
