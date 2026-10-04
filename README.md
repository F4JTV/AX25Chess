# AX25Chess

Point-to-point chess over **AX.25 packet radio**. Two stations, one game, no
server in between. The modem is **Dire Wolf**, compiled into the program: a
sound card and a PTT line are all the hardware needed.

One code base, one Qt Quick interface, three platforms: **Windows**,
**Linux** (Ubuntu 24.04) and **Android**. On a wide screen the board and its
panels sit side by side; on a phone, pages under a tab bar.

Version 2 is a rewrite in C++ of the Python version 1. The protocol on the
air (CHS-1) is unchanged: the two versions play against each other.

---

## How it works

Each piece carries a **unique identifier** that never changes for the whole
game, and each square carries a **unique number from 1 to 64**. A move on the
air is therefore exactly three things: **callsign, piece identifier,
destination square**.

### Piece identifiers

```
WR1 WN1 WB1 WQ1 WK1 WB2 WN2 WR2      BR1 BN1 BB1 BQ1 BK1 BB2 BN2 BR2
WP1 .. WP8                           BP1 .. BP8
```

Colour letter, original piece type, index. A promotion **keeps** the
identifier: `WP5` promoted to a queen is still `WP5`, only its type changes.
Traceability is absolute from the first move to the last — the move list shows
which pawn became that queen.

### Square numbering

`number = (rank - 1) x 8 + file + 1`, so **a1 = 1**, **h1 = 8**, **h8 = 64**.

```
  8 | 57 58 59 60 61 62 63 64
  7 | 49 50 51 52 53 54 55 56
  6 | 41 42 43 44 45 46 47 48
  5 | 33 34 35 36 37 38 39 40
  4 | 25 26 27 28 29 30 31 32
  3 | 17 18 19 20 21 22 23 24
  2 |  9 10 11 12 13 14 15 16
  1 |  1  2  3  4  5  6  7  8
      a  b  c  d  e  f  g  h
```

### Why the game stays consistent

The channel is slow, half-duplex and unreliable. Seven mechanisms combine to
keep both boards identical.

**1. Stop-and-wait comes for free.** Only the player to move may send a move,
and there is never more than one reliable frame in flight. Chess alternates:
the structure of the game *is* the flow control. No sliding window, no
collision between two moves.

**2. Half-move numbering.** Every `MOVE` carries its ply index. The receiver
accepts only `ply == expected`: a lower ply is a duplicate (acknowledged again
without replaying), a higher ply is a gap and triggers a resynchronisation.

**3. A position fingerprint inside every move.** The sender plays the move
locally and attaches the CRC16 of the resulting position. The receiver applies
the move and compares. **Divergence is caught within one half-move**, not ten
moves later when nothing can be recovered. This is the single most important
mechanism in the protocol.

**4. Both sides validate, no arbiter.** Both stations run the same rules
engine. An incoming move is checked against the local list of legal moves
before being applied. Chess being deterministic, no master station is needed:
the two states cannot diverge silently.

**5. Acknowledgement and timed retransmission.** Every 14 s by default, with
random jitter to avoid collisions, six attempts, then the operator is warned.
A duplicate is always acknowledged again: idempotence prevents a deadlock when
it was the ACK that was lost.

**6. Resynchronisation replays the history, it does not send a snapshot.**
`SYNC` carries the full move list in compact form, split into blocks. The
receiver replays everything from the initial position, which rebuilds not only
the position but also the piece identifiers, castling rights, the en-passant
square and the repetition history — all of which a bare FEN would lose. The
local history must be a **prefix** of the received one, otherwise the
divergence is declared irreconcilable rather than papered over.

**7. Colour assignment without an arbiter.** Each station draws a 32-bit nonce
in `HELLO` / `ACPT`; the higher nonce plays White, ties broken by callsign
alphabetical order. Both stations reach the same conclusion with no further
negotiation.


The full specification is in [docs/PROTOCOL.md](docs/PROTOCOL.md).

---

## Using it

1. **Settings -> Station**: your callsign (with its SSID if you use one) and
   your correspondent's. Digipeaters only if the path needs them; each relay
   adds delay, so raise the retransmission delay (Settings -> Game) as well.
2. **Settings -> Modem**: the sound card in and out, the speed (1200 baud on
   VHF/UHF, 300 on HF), the PTT and TXDELAY. Save: the program writes the
   matching `direwolf.conf` and restarts the modem.
3. **Start** in the header starts the station; the MODEM indicator turns
   green once Dire Wolf runs. CHANNEL shows RX while a carrier is heard and TX
   while transmitting.
4. **Start a game** sends the invitation. Colours are drawn on both sides
   at once. Tap a piece, then its square. Tapping the king and then its own
   rook castles.

The header's TURN indicator is green when it is your move, amber while you
wait. The status line (wide screens) shows the acknowledgement being waited
for and the position fingerprint, or the number, name and occupant of the
square under the pointer.

**Saved games.** A game is saved after every half-move and removed when it
ends; any number can run side by side, one per correspondent and game id.
*Saved games* resumes one; the station settings follow the game resumed.

**Messages** go to the correspondent as CHAT frames, acknowledged and
retransmitted like moves, 180 characters at most each.

**Logs.** *Frames* is the protocol: every frame sent (`>`) and received
(`<`), retransmissions, resynchronisations. *Modem* is Dire Wolf's own
console.

## The modem

Dire Wolf 1.8 runs inside the program on its own threads: demodulators,
HDLC/FX.25/IL2P decoding, the transmit queue with its p-persistence channel
access, PTT. What the stand-alone program does around it (KISS and AGW
servers, digipeater, IGate, beacons) is left out. The core is the one of
AX25Chat, carried over unchanged, with the patch in
`patches/direwolf/0001-embedded-host-shutdown.patch` that lets the modem stop
and start again without leaking (a change of sound card in Settings does
exactly that).

### The configuration file

By default the program writes `direwolf.conf` itself, in the settings folder,
from Settings -> Modem: `ADEVICE`, `ARATE`, `CHANNEL 0`, `MYCALL`, `MODEM`,
the `PTT` line, `TXDELAY`, `TXTAIL`, `PERSIST`, `SLOTTIME` and `FULLDUP`.
*Show the generated direwolf.conf* displays it. On the desktop, *My own
direwolf.conf* uses a file of your own instead (*Create a starter file*
writes one to edit; *Edit* opens it in the program); the Dire Wolf User Guide
documents every keyword. Keywords of the parts left out (`KISSPORT`,
`AGWPORT`, `DIGIPEAT`, `IGSERVER`, `PBEACON`...) are accepted and ignored.

Sound cards: on Linux the list comes from ALSA, by card id
(`plughw:CARD=Device,DEV=0`), which stays right across reboots, unlike card
numbers; *System default* goes through PipeWire or PulseAudio. On Windows,
the device numbers Dire Wolf uses. On Android, the phone's audio: Android
switches to a USB sound card or headset by itself when one is plugged in.

PTT:

| Choice | Line written | Notes |
|---|---|---|
| None (VOX) | none | VOX releases late and the next station may be doubled: a real PTT is better on packet |
| Serial adapter, RTS / DTR | `PTT /dev/ttyUSB0 RTS` (`COM3` on Windows) | the port is set in Settings |
| USB sound card GPIO (CM108) | `PTT CM108 3` | Digirig and most USB interfaces; GPIO 3 is the usual pin |

On Linux the CM108 GPIO is reached through `/dev/hidraw*`: the Debian
package installs a udev rule that gives it to the `audio` group (your user
must be in it). On Android both kinds of USB adapter are driven from Java
(`UsbPtt.java`, from AX25Chat); Android asks once for the permission to use
the device.

**Every frame gets its full TXDELAY.** A frame handed to the modem holds the
queue until the transmitter has keyed and released, then the inter-frame
gap: Dire Wolf would otherwise bundle a second frame queued at the same
moment into the same keying, with a few flags in place of the TXDELAY. (Found
and fixed in AX25Chat; the channel manager and its tests are carried over.)

### On a phone

- The microphone permission is asked at the first start; without it the
  modem cannot receive.
- A foreground service (notification "AX25Chess is listening") keeps the
  modem running with the screen off; Settings -> Phone switches it off, and
  asks for the battery optimisation exemption that Doze otherwise needs.
- Portrait only: the board and its pages are laid out for a phone held
  upright, and the application never turns to landscape.
- The screen stays on while AX25Chess is in front (Settings -> Phone).
- A move, a message or a draw offer received while the application is in the
  background raises a notification.

## Building

### Linux (Ubuntu 24.04)

```bash
./build_linux.sh --deps --test --run   # build dependencies, build, tests, start
./make_deb.sh                          # ax25chess_<version>_<arch>.deb
sudo apt install ./build-deb/ax25chess_*.deb
```

Ubuntu's own Qt 6.4 is used; nothing in the program needs more. The scripts
fetch Dire Wolf 1.8 from GitHub and apply the patch the first time
(`scripts/fetch_direwolf.sh`). By hand:

```bash
sudo apt install build-essential cmake ninja-build git patch qt6-base-dev \
    qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates \
    qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    libasound2-dev libudev-dev
scripts/fetch_direwolf.sh
cmake -B build -G Ninja && cmake --build build
```

The QML modules are loaded at run time and linked by nothing, so no tool
can derive them: the package declares them, or the program would open an
empty window and say nothing.

### Windows

Install once: Visual Studio 2022 or 2026 with *Desktop development with
C++* **and** the individual component *C++ Clang Compiler for Windows*; Qt 6
for MSVC 64-bit (the base installation has Qt Quick); git; Inno Setup 6 or
7. Then, in an *x64 Native Tools Command Prompt*, from the project folder:

```bat
build_all.bat /deps
```

The result is `installer\output\AX25Chess-<version>-setup.exe`, English and
French. Edit `QT_DIR` at the top of the script if Qt is not in
`C:\Qt\6.11.2\msvc2022_64`. Options: `/deps` (fetch Dire Wolf), `/clean`,
`/nobuild`, `/noinstaller`, `/test`.

Things that look odd and are deliberate, all learned on AX25Chat:

- **clang-cl compiles everything.** Dire Wolf's sources use GCC extensions
  that `cl.exe` rejects; clang-cl accepts them and produces MSVC objects that
  link with Qt's MSVC binaries. CMake will not mix clang-cl and `cl` in one
  project, so the C++ goes through clang-cl too.
- **`/MANIFESTUAC:NO`** at link time: lld-link would merge its own trustInfo
  block into Qt's manifest in a form Windows rejects ("side-by-side
  configuration is incorrect", error 14001).
- **`windeployqt --qmldir src\qml`**: the QML is compiled into the program,
  so windeployqt has to read the imports from the sources.
- **Dates in the future.** Files dated later than the computer's clock
  make Ninja re-run CMake without end ("Re-running CMake..." over and
  over). The usual cause is Windows reading the hardware clock as local time
  on a PC that also runs Linux, which keeps it in UTC: Windows then runs two
  hours behind until it synchronises. `build_all.bat` sets such files to now
  (`scripts\fix_timestamps.ps1`) and, should build.ninja still be out of date
  after configuring, stops with Ninja's own explanation instead of looping.
  The lasting cure is one of `timedatectl set-local-rtc 1` on Linux, or the
  `RealTimeIsUniversal` registry value on Windows.
- `build_all.bat` and `installer\AX25Chess.iss` must keep Windows line
  endings (`.gitattributes` pins them; `scripts/check_line_endings.sh` and
  `scripts/check_batch.py` check them and the cmd.exe traps).

The installer upgrades an installation of the Python version 1 in place and
removes its Python runtime. A Dire Wolf that version installed beside it is
a program of its own and is left alone; AX25Chess no longer needs it.

### Android

From Ubuntu 24.04, with a phone connected by USB (developer mode, USB
debugging on):

```bash
./build_android.sh --setup --install   # the first time
./build_android.sh --install           # afterwards
./build_android.sh --logcat            # install and follow the log
```

`--setup` installs the JDK, adb, Qt for Android with the host Qt it needs
(aqtinstall), the SDK platform, build tools and NDK, and creates the signing
key (keep it: an update must carry the same key). The NDK version must be the
one the Qt release was built with; the script checks that the platform and
the NDK are there before building. The manifest is derived on every build
from Qt's own template for the Qt installed, with the service, the splash
screen and `android.app.background_running` set to true (without it Qt
suspends the event loop when the phone locks, and with it the game's clock),
and checked as XML before it is handed on.

## Tests

```bash
./build_linux.sh --test
```

| Test | What it checks |
|---|---|
| `chess` | move generation (perft on six positions), games, fingerprints and frames byte for byte against the Python version, a game on a loopback with 35 % of the frames lost, resynchronisation, duplicates, saved games |
| `radio` | the generated configuration, the sound card list, channel access (from AX25Chat), the modem started, used and stopped three times |
| `modem` | a game through the real modem: frames made into audio by Dire Wolf's `gen_packets`, decoded by the embedded core (run when `gen_packets` is on `PATH`) |
| `ui` | the interface under real pointer events, wide and phone layouts: a move played by clicking the board; every control of every page inside a 360 and a 412 dp wide phone, in French; every text has its French |

## Files

| | Linux | Windows | Android |
|---|---|---|---|
| Settings, `direwolf.conf` | `~/.config/AX25Chess` | `%LOCALAPPDATA%\AX25Chess` | private storage |
| Games in progress | `~/.local/share/AX25Chess/games` | `%APPDATA%\AX25Chess\games` | private storage |

The games of the Python version (`~/.ax25chess/parties`) are imported once
at start-up on the desktop.

## Source layout

```
src/chess/      rules (Board), CHS-1 (GameSession), saved games (GameStore)
src/radio/      the modem's lifecycle and channel access (RadioLink, ChannelManager)
src/engine/     the Qt engine around the core; Android PTT and notifications
src/core/       the embedded Dire Wolf core (C), Oboe audio and PTT for Android
src/protocol/   AX.25 frames
src/app/        the application: controller, board view, models, French catalog
src/qml/        the interface
android/        the Java side and the launcher icon
patches/        the changes to Dire Wolf's sources
```

## Licence

GNU General Public License, version 2 or later (`LICENSE.txt`, `COPYING`).
Dire Wolf is by John Langner, WB2OSZ, under the same licence; Qt is under
the LGPL version 3, so binaries are distributed under the terms of GPL
version 3. The piece font is a subset of DejaVu Sans
(`assets/fonts/LICENSE.txt`).
