#!/usr/bin/env bash
# ============================================================================
#  AX25Chess - Linux build for development
#
#  Installs the build dependencies when asked, fetches and patches the Dire
#  Wolf sources on the first run, configures, compiles, and runs the unit
#  tests. The result stays in build/; nothing is installed. For a package,
#  see make_deb.sh.
#
#  Usage:
#    ./build_linux.sh              configure and compile
#    ./build_linux.sh --test       ... and run the tests (ctest)
#    ./build_linux.sh --run        ... and start the program
#    ./build_linux.sh --deps       install the build dependencies first (sudo)
#    ./build_linux.sh --no-deps    never check them, and do not ask
#    ./build_linux.sh --debug      a Debug build instead of Release
#    ./build_linux.sh --clean      start from an empty build directory
#    ./build_linux.sh --help
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build"
BUILD_TYPE=Release
DO_TEST=0
DO_RUN=0
DO_CLEAN=0
DO_DEPS=ask          # ask | yes | no

# shellcheck source=packaging/build-deps.sh
. "$SRC_DIR/packaging/build-deps.sh"

while [ $# -gt 0 ]; do
    case "$1" in
        --test)    DO_TEST=1 ;;
        --run)     DO_RUN=1 ;;
        --deps)    DO_DEPS=yes ;;
        --no-deps) DO_DEPS=no ;;
        --debug)   BUILD_TYPE=Debug ;;
        --clean)   DO_CLEAN=1 ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

step "Checking prerequisites"
[ -f "$SRC_DIR/CMakeLists.txt" ] || die "Run this from the project directory."

AX_DEPS_MODE="$DO_DEPS"
ax_install_deps || true
command -v cmake >/dev/null 2>&1 || die "cmake missing: sudo apt install cmake"
command -v ninja >/dev/null 2>&1 || die "ninja missing: sudo apt install ninja-build"

ax_ensure_direwolf "$SRC_DIR"


if [ "$DO_CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    step "Cleaning"
    rm -rf "$BUILD_DIR"
    say "$BUILD_DIR removed"
fi

step "Building ($BUILD_TYPE)"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" >/dev/null \
    || die "Configuration failed."
cmake --build "$BUILD_DIR" -j"$(ax_build_jobs)" || die "Compilation failed."
say "Program: $BUILD_DIR/ax25chess"

if [ "$DO_TEST" -eq 1 ]; then
    step "Tests"
    ( cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure ) || die "A test failed."
fi

if [ "$DO_RUN" -eq 1 ]; then
    step "Starting"
    exec "$BUILD_DIR/ax25chess"
fi
