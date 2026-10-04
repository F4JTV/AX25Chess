# ============================================================================
#  AX25Chess - build dependencies on Debian-based systems
#
#  Sourced by build_linux.sh and make_deb.sh: one list to keep up to date.
#  Written for Ubuntu 24.04 (Qt 6.4); Debian 13 and derivatives work the
#  same way.
#
#  Variables the caller may set before calling ax_install_deps:
#    AX_DEPS_MODE   ask | yes | no    (default ask)
#    AX_EXTRA_DEPS  more packages, e.g. "dpkg-dev lintian"
# ============================================================================

# What the compile needs. Multimedia is optional at build time (custom
# notification sounds fall back to the system beep without it) but cheap,
# so it is in the list; hamlib is not: PTT through rigctld is a niche need
# and its dependency would land on every installation.
AX_BUILD_DEPS="build-essential cmake ninja-build git patch pkg-config
qt6-base-dev qt6-declarative-dev
qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts
qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript
libasound2-dev libudev-dev libgl1-mesa-dev"

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

ax_missing_packages() {
    local want="$1" miss=""
    for pkg in $want; do
        dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed" \
            || miss="$miss $pkg"
    done
    printf '%s' "$miss"
}

# Install what is missing. Returns 0 when the build can go ahead, 1 when
# something is missing and was not installed.
ax_install_deps() {
    [ "${AX_DEPS_MODE:-ask}" = "no" ] && return 0

    command -v apt-get >/dev/null 2>&1 || {
        say "Not a Debian-based system; install the dependencies yourself."
        return 1
    }
    command -v dpkg-query >/dev/null 2>&1 || return 1

    local want="$AX_BUILD_DEPS ${AX_EXTRA_DEPS:-}"
    local miss
    miss="$(ax_missing_packages "$want")"
    if [ -z "$miss" ]; then
        say "All build dependencies are already installed."
        return 0
    fi

    step "Build dependencies"
    say "Missing:$miss"

    if [ "${AX_DEPS_MODE:-ask}" = "ask" ]; then
        # Without a terminal to answer on, do not block: say what to do.
        if [ ! -t 0 ]; then
            say "Install them with: sudo apt install$miss"
            return 1
        fi
        printf "  Install them now? [Y/n] "
        read -r reply
        case "$reply" in [nN]*) say "Skipped."; return 1 ;; esac
    fi

    local sudo_cmd=""
    [ "$(id -u)" -eq 0 ] || sudo_cmd="sudo"
    if [ -n "$sudo_cmd" ] && ! command -v sudo >/dev/null 2>&1; then
        say "No sudo available. Install them with: apt install$miss"
        return 1
    fi

    $sudo_cmd apt-get update || say "apt-get update failed, carrying on anyway"
    # shellcheck disable=SC2086
    $sudo_cmd apt-get install -y $miss || die "Installing the dependencies failed."
    say "Done."
    return 0
}

# Parallel jobs, bounded by memory and not by cores alone: a Qt compile
# takes about 700 MiB per job, and a Raspberry Pi with 2 GiB running four
# of them gets killed by the kernel on the way.
ax_build_jobs() {
    local jobs mem_kb by_mem
    jobs="$(nproc 2>/dev/null || echo 1)"
    mem_kb="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
    if [ "$mem_kb" -gt 0 ]; then
        by_mem=$(( mem_kb / 700000 ))
        [ "$by_mem" -lt 1 ] && by_mem=1
        if [ "$by_mem" -lt "$jobs" ]; then
            say "Limiting to $by_mem parallel job(s): $(( mem_kb / 1024 )) MiB of RAM" >&2
            jobs="$by_mem"
        fi
    fi
    printf '%s' "$jobs"
}

# The Dire Wolf sources, fetched and patched by scripts/fetch_direwolf.sh
# when they are not there yet.
ax_ensure_direwolf() {
    local src="$1"
    if [ -f "$src/external/direwolf/src/direwolf.h" ] && grep -q tq_term "$src/external/direwolf/src/tq.h"; then
        return 0
    fi
    step "Dire Wolf sources"
    command -v git >/dev/null 2>&1 || die "git missing: sudo apt install git"
    command -v patch >/dev/null 2>&1 || die "patch missing: sudo apt install patch"
    "$src/scripts/fetch_direwolf.sh" || die "Could not fetch the Dire Wolf sources."
}
