#!/usr/bin/env bash
# ============================================================================
#  AX25Chess - Android build from Ubuntu 24.04
#
#  Builds AX25Chess (the Qt Quick interface) as an APK, signs it and
#  installs it on the phone connected by USB. The first time, --setup
#  installs everything needed: the JDK, adb, Qt for Android and the desktop
#  Qt it needs as host tools (aqtinstall in a Python virtual environment),
#  the Android SDK platform, build tools and NDK (sdkmanager), and creates
#  the signing key. What is already installed is kept, so it can be run again.
#
#  Keep the signing key: an update must carry the same key as the version it
#  replaces, or Android refuses to install it.
#
#  Usage:
#    ./build_android.sh --setup --install   first time
#    ./build_android.sh --install           build, sign, install
#    ./build_android.sh --reinstall         ... removing the previous install first
#    ./build_android.sh --logcat            ... and follow the application's log
#    ./build_android.sh --no-sign           stop at the unsigned APK
#    ./build_android.sh --clean             start from an empty build directory
#    ./build_android.sh --help
#
#  Paths and versions are variables below, each overridable from the
#  environment (QT_VERSION=6.9.1 ./build_android.sh ...).
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-android"

QT_VERSION="${QT_VERSION:-6.11.2}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
QT_ARCH="${QT_ARCH:-android_arm64_v8a}"
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
PLATFORM="${ANDROID_PLATFORM:-36}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-35.0.0}"
CMDLINE_TOOLS_URL="${CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip}"
KEYSTORE="${KEYSTORE:-$HOME/.ax25chess-android.keystore}"
KEY_ALIAS="${KEY_ALIAS:-ax25chess}"
KEY_PASS="${KEY_PASS:-ax25chess}"
VENV="$SRC_DIR/.venv-android"
PACKAGE="org.ax25chess"

DO_SETUP=0; DO_INSTALL=0; DO_REINSTALL=0; DO_LOGCAT=0; DO_SIGN=1; DO_CLEAN=0

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --setup)     DO_SETUP=1 ;;
        --install)   DO_INSTALL=1 ;;
        --reinstall) DO_INSTALL=1; DO_REINSTALL=1 ;;
        --logcat)    DO_INSTALL=1; DO_LOGCAT=1 ;;
        --no-sign)   DO_SIGN=0 ;;
        --clean)     DO_CLEAN=1 ;;
        -h|--help)   sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

QT_ANDROID="$QT_ROOT/$QT_VERSION/$QT_ARCH"
QT_HOST="$QT_ROOT/$QT_VERSION/gcc_64"
NDK_ROOT="$SDK_ROOT/ndk/$NDK_VERSION"

# ------------------------------------------------------------------- setup
if [ "$DO_SETUP" -eq 1 ]; then
    step "System packages"
    sudo apt-get update
    sudo apt-get install -y openjdk-17-jdk-headless git unzip curl python3-venv adb ninja-build cmake

    step "Qt $QT_VERSION for Android and the host tools (aqtinstall)"
    if [ ! -d "$VENV" ]; then python3 -m venv "$VENV"; fi
    # shellcheck disable=SC1091
    . "$VENV/bin/activate"
    pip install --quiet --upgrade pip aqtinstall
    if [ ! -x "$QT_HOST/bin/qmake" ]; then
        aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT"
    fi
    if [ ! -d "$QT_ANDROID/lib" ]; then
        aqt install-qt linux android "$QT_VERSION" "$QT_ARCH" -O "$QT_ROOT"
    fi
    # Qt Quick and Quick Controls are in the base package: no -m switch.
    deactivate

    step "Android SDK, build tools and NDK"
    if [ ! -x "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" ]; then
        mkdir -p "$SDK_ROOT/cmdline-tools"
        tmp="$(mktemp -d)"
        curl -fsSL -o "$tmp/tools.zip" "$CMDLINE_TOOLS_URL"
        unzip -q "$tmp/tools.zip" -d "$tmp"
        rm -rf "$SDK_ROOT/cmdline-tools/latest"
        mv "$tmp/cmdline-tools" "$SDK_ROOT/cmdline-tools/latest"
        rm -rf "$tmp"
    fi
    yes | "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK_ROOT" --licenses >/dev/null || true
    "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK_ROOT" \
        "platform-tools" "platforms;android-$PLATFORM" "build-tools;$BUILD_TOOLS" "ndk;$NDK_VERSION" \
        || die "sdkmanager failed."

    step "Signing key"
    if [ ! -f "$KEYSTORE" ]; then
        keytool -genkeypair -v -keystore "$KEYSTORE" -alias "$KEY_ALIAS" -keyalg RSA -keysize 2048 \
            -validity 10000 -storepass "$KEY_PASS" -keypass "$KEY_PASS" -dname "CN=AX25Chess" \
            || die "keytool failed."
        say "Created $KEYSTORE - keep it, updates must be signed with the same key."
    else
        say "Using $KEYSTORE"
    fi
    if [ -f /lib/udev/rules.d/51-android.rules ] || [ -f /etc/udev/rules.d/51-android.rules ]; then
        say "adb udev rules present"
    else
        say "adb: if the phone is not seen, install android-sdk-platform-tools-common (udev rules)."
    fi
fi

# ------------------------------------------------------------- prerequisites
step "Checking prerequisites"
[ -x "$QT_ANDROID/bin/qt-cmake" ] || die "Qt for Android not found at $QT_ANDROID (run --setup, or set QT_ROOT/QT_VERSION/QT_ARCH)."
[ -d "$QT_HOST/bin" ] || die "Host Qt not found at $QT_HOST (run --setup)."
[ -d "$NDK_ROOT" ] || die "NDK $NDK_VERSION not found in $SDK_ROOT/ndk (run --setup)."
[ -d "$SDK_ROOT/platforms/android-$PLATFORM" ] || die "Android platform $PLATFORM not found (run --setup)."
[ -d "$QT_ANDROID/lib/cmake/Qt6Quick" ] || die "Qt Quick missing from $QT_ANDROID: reinstall the base package (--setup)."
command -v java >/dev/null 2>&1 || die "java missing: sudo apt install openjdk-17-jdk-headless"
if [ ! -f "$SRC_DIR/external/direwolf/src/direwolf.h" ]; then
    "$SRC_DIR/scripts/fetch_direwolf.sh" || die "Could not fetch the Dire Wolf sources."
fi
VERSION="$(sed -n 's/^project(AX25Chess VERSION \([0-9.]*\).*/\1/p' "$SRC_DIR/CMakeLists.txt" | head -1)"
say "Version     ${VERSION:-unknown}"
say "Qt          $QT_ANDROID"
say "NDK         $NDK_ROOT"
say "ABI         $ANDROID_ABI"


# The manifest: Qt's own template for this Qt version, with the splash
# screen added (Qt reads android.app.splash_screen_drawable from the
# activity's meta-data; there is no CMake property for it).  Generated on
# every run so it never lags behind the Qt installed.
step "Manifest"
TEMPLATE="$QT_ANDROID/src/android/templates/AndroidManifest.xml"
GENERATED="$SRC_DIR/android/AndroidManifest.xml"
rm -f "$GENERATED"
if [ -f "$TEMPLATE" ]; then
    python3 - "$TEMPLATE" "$GENERATED" <<'PYEOF'
import re, sys
import xml.etree.ElementTree as ET
template, target = sys.argv[1], sys.argv[2]
text = open(template, encoding="utf-8").read()
# The splash meta-data belongs to the activity: put it just before the
# activity's closing tag, whatever the layout of the template (the
# meta-data elements themselves may span several lines).
marker = re.search(r'^([ \t]*)</activity>', text, re.M)
if marker is None:
    sys.exit("no </activity> in the template")
indent = marker.group(1) + "    "
insert = (f"{indent}<!-- Added by build_android.sh from Qt's template: the splash screen, android/res/drawable/splash.xml. -->\n"
          f'{indent}<meta-data android:name="android.app.splash_screen_drawable" android:resource="@drawable/splash"/>\n'
          f'{indent}<meta-data android:name="android.app.splash_screen_sticky" android:value="false"/>\n')
# Qt suspends an application's event loop when its activity leaves the
# screen unless background_running is true: with it false (the template's
# default), every timer, socket and queued signal on the Qt thread stops
# the moment the phone locks - the retransmission clock of the game among
# them (learned the hard way in AX25Chat).  Set to
# true, whether the template carries the entry or not.
bg = re.compile(r'(<meta-data\s+android:name="android.app.background_running"\s+android:value=")false(")', re.S)
if bg.search(text):
    text = bg.sub(r'\1true\2', text)
else:
    insert += (f"{indent}<!-- Added by build_android.sh: keep the Qt event loop running off screen. -->\n"
               f'{indent}<meta-data android:name="android.app.background_running" android:value="true"/>\n')
result = text[:marker.start()] + insert + text[marker.start():]
# Portrait only, never landscape: the activity's orientation is fixed,
# whether the template sets one (Qt's says "unspecified") or not.
orient = re.compile(r'android:screenOrientation="[^"]*"')
activity = re.search(r'<activity\b[^>]*>', result, re.S)
if activity is None:
    sys.exit("no <activity> in the template")
tag = activity.group(0)
if orient.search(tag):
    tag = orient.sub('android:screenOrientation="portrait"', tag)
else:
    tag = re.sub(r'<activity\b', '<activity android:screenOrientation="portrait"', tag, count=1)
result = result[:activity.start()] + tag + result[activity.end():]
# The foreground service that keeps the application running off screen
# (KeepAliveService.java), declared with every type it may use; the
# service picks, at run time, the ones whose permissions were granted.
app_end = re.search(r'^([ \t]*)</application>', result, re.M)
if app_end is None:
    sys.exit("no </application> in the template")
indent = app_end.group(1) + "    "
service = (f"{indent}<!-- Added by build_android.sh: the foreground service of android/src/org/ax25chess/KeepAliveService.java. -->\n"
           f'{indent}<service android:name="org.ax25chess.KeepAliveService" android:exported="false"\n'
           f'{indent}         android:foregroundServiceType="microphone|specialUse|dataSync">\n'
           f'{indent}    <property android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE"\n'
           f'{indent}              android:value="Amateur packet radio modem kept listening for chess moves from the correspondent"/>\n'
           f'{indent}</service>\n')
result = result[:app_end.start()] + service + result[app_end.start():]
# Refuse to hand androiddeployqt something it cannot parse.
ET.fromstring(result.encode("utf-8"))
open(target, "w", encoding="utf-8").write(result)
PYEOF
    if [ -f "$GENERATED" ]; then
        say "Qt's template with the splash screen -> android/AndroidManifest.xml"
    else
        say "[--] Could not derive a manifest from Qt's template; building without a splash screen."
    fi
else
    say "[--] Qt's manifest template not found at $TEMPLATE; building without a splash screen."
fi

if [ "$DO_CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    step "Cleaning"
    rm -rf "$BUILD_DIR"
fi

# --------------------------------------------------------------------- build
step "Configuring"
SIGN_ARGS=()
if [ "$DO_SIGN" -eq 1 ]; then
    [ -f "$KEYSTORE" ] || die "Signing key $KEYSTORE not found (run --setup, or --no-sign)."
    export QT_ANDROID_KEYSTORE_PATH="$KEYSTORE"
    export QT_ANDROID_KEYSTORE_ALIAS="$KEY_ALIAS"
    export QT_ANDROID_KEYSTORE_STORE_PASS="$KEY_PASS"
    export QT_ANDROID_KEYSTORE_KEY_PASS="$KEY_PASS"
    SIGN_ARGS=(-DQT_ANDROID_SIGN_APK=ON)
fi
"$QT_ANDROID/bin/qt-cmake" -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_SDK_ROOT="$SDK_ROOT" \
    -DANDROID_NDK_ROOT="$NDK_ROOT" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DAX25CHESS_BUILD_TESTS=OFF \
    "${SIGN_ARGS[@]}" || die "Configuration failed."

step "Compiling and packaging"
cmake --build "$BUILD_DIR" --target apk || die "Build failed."

APK="$(find "$BUILD_DIR/android-build" -name '*.apk' -newer "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1)"
[ -n "$APK" ] || APK="$(find "$BUILD_DIR/android-build" -name '*.apk' 2>/dev/null | head -1)"
[ -n "$APK" ] || die "No APK produced."
# Named like the other installers: AX25Chess-<version>-<abi>.apk, next to
# AX25Chess-<version>-setup.exe and ax25chess_<version>_<arch>.deb.  Older
# copies of another version are left in place.
OUT_APK="$SRC_DIR/AX25Chess-${VERSION:-0.0.0}-$ANDROID_ABI.apk"
rm -f "$SRC_DIR/ax25chess-$ANDROID_ABI.apk"
cp -f "$APK" "$OUT_APK"
step "Result"
say "$OUT_APK ($(du -h "$APK" | cut -f1))"

# ------------------------------------------------------------------- install
if [ "$DO_INSTALL" -eq 1 ]; then
    step "Installing on the phone"
    command -v adb >/dev/null 2>&1 || die "adb missing: sudo apt install adb"
    adb devices | grep -q -w device || die "No phone connected (USB debugging on, and authorised?)."
    if [ "$DO_REINSTALL" -eq 1 ]; then adb uninstall "$PACKAGE" >/dev/null 2>&1 || true; fi
    adb install -r "$APK" || die "adb install failed."
    adb shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
    say "Installed and started."
    if [ "$DO_LOGCAT" -eq 1 ]; then
        step "Log (Ctrl-C to stop)"
        adb logcat -c
        adb logcat | grep -E "ax25chess|AX25Chess|libc:|DEBUG|AndroidRuntime"
    fi
fi
