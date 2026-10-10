# Changelog

All notable changes to AX25Chess. Versions follow `CMakeLists.txt`;
`bump_version.sh` updates this file, the Debian changelog and the project
version together.

## 2.0.6 - 2026-10-10

- Android, receive: frames of the other station, its ACKs among them, were often lost while the desktop decoded them all. The receive chain is AX25Chat's, byte for byte (Dire Wolf core, Oboe backend, build flags); the one thing in it that differs from the desktop is how much audio the input keeps. The Oboe backend opened the input as a low-latency stream, which keeps a few milliseconds (a FAST track or an MMAP buffer), while Dire Wolf's receive thread demodulates a 43 ms block between two reads; any pause longer than that buffer, on a phone that slows its CPU down when idle, drops audio, and at 1200 baud a gap of a few ms costs a frame its CRC. On the desktop ALSA keeps a buffer many periods long. The input is now opened on Android's normal path with a second of buffer (room, not delay: a reader that keeps up finds it empty); the output stays low-latency. The modem log gives the mode and buffer each stream obtained, and reports any overrun of the input, which tells whether this was the cause on a given phone.
- Android: the sample rate is fixed at 48000 Hz, as AX25Chat does: the phone's audio runs at that rate and any other one had Oboe resample the input and the output. Settings no longer offers the choice on Android; a rate chosen with an earlier version is brought back to 48000.
- A frame from the correspondent addressed to another callsign was dropped in silence, which looked like a frame the radio never decoded; the Frames log now says so and asks to check the callsigns.

## 2.0.5 - 2026-10-08

- Android: frames sent from the phone lost their end, the FCS and the closing flag, so the other station could not decode them - the ACKs among them, and an invitation was never answered. The phone plays its sound a whole output latency after it is written (40 to over 100 ms) and the PTT was released as soon as the buffer had been handed over. audio_wait() now waits for Oboe's latency estimate (calculateLatencyMillis), or the buffer's duration plus 60 ms without one.
- TXTAIL defaults to 100 ms, Dire Wolf's own default, instead of 50; a configuration written by 2.0.3 or older and still at 50 ms is raised once, and the Frames log says so.
- Android: the receiver is muted while transmitting in half duplex, as Dire Wolf's ptt.c does; the phone used to decode its own frames.
- CHS-1, receive side: a reliable frame counts as received only once acknowledged (a rejected move that was repeated got an ACK without being played); a move arriving after the end of the game is acknowledged, not played; a HELLO drops the previous game's frames, which held the ACPT back for ever; crossed invitations end in one game, the one with the smaller id. docs/PROTOCOL.md describes them.
- Windows: the program died as soon as the modem started with PTT CM108 and no device path. Dire Wolf's CM108 inventory passed hidapi's product string to wcstombs() without checking it, and hidapi leaves it NULL for devices whose string Windows refuses; the Microsoft C runtime ends the process on a NULL source. patches/direwolf/0002-cm108-null-product-string.patch checks it. The build scripts now apply, on every run, any patch an existing Dire Wolf tree lacks.
- The modem's console is also written to the program's console (logcat on Android), so its last lines survive a crash.

## 2.0.3 - 2026-10-04

- Phone: controls of Settings stuck out past the right edge in portrait. In a Qt Quick layout an item that does not fill the width keeps its implicit width, and a switch or a button is as wide as its label on one line: the long (French) ones widened their whole card. Switches and buttons of Settings now take the column's width and wrap their labels; the buttons under the board and on the Game page share the width equally and shrink their type rather than break a word; the header's title gives way first.
- Android: portrait only. The generated manifest fixes the activity's orientation, whether Qt's template sets one or not.
- Tests: every control of every page must fit a 360 and a 412 dp wide phone held upright, in French.

## 2.0.2 - 2026-10-04

- Windows: the build looped on "Re-running CMake..." when the project's files were dated later than the computer's clock (typically Windows two hours behind on a PC that also runs Linux). build_all.bat now sets such files to now (scripts/fix_timestamps.ps1), and stops with Ninja's explanation if build.ninja is still out of date after configuring.

## 2.0.1 - 2026-10-04

- Android: the manifest step of build_android.sh failed on an apostrophe inside a Python string, so the APK would have lost its foreground service, its splash screen and background_running.
- Android: the configuration stopped on install(TARGETS) without a LIBRARY destination; on Android the program is a shared library packaged by androiddeployqt, and the install rule now applies to the desktop only.

## 2.0.0 - 2026-10-04

- Rewritten in C++17 with Qt 6 and one Qt Quick interface for Windows,
  Linux (Ubuntu 24.04) and Android: the board and its panels side by side
  on a wide screen, pages under a tab bar on a phone.
- Dire Wolf 1.8 is compiled into the program (the embedded core of
  AX25Chat): no separate modem to install or start, no KISS link. The
  modem is configured in Settings and its `direwolf.conf` generated; a file
  of one's own can still be used on the desktop.
- Android: audio through Oboe, PTT through a USB sound card (CM108 GPIO) or
  a USB serial adapter (RTS/DTR), a foreground service so the station keeps
  listening with the screen off.
- The CHS-1 protocol is unchanged: frames, fingerprints and compact
  histories are byte for byte those of the Python version, which this one
  plays against (checked by tests on vectors produced by the Python code).
- Fixed: a second invitation from the same correspondent was taken for a
  duplicate of the first one's HELLO (the inviting side starts its frame
  counter again for every game) and never answered.
- Fixed: the "new message" mark went to the wrong tab.
- Castling by tapping the king and then its rook.
- Four themes: dark, light for direct sunlight, red for the night, amber
  for dim light.
- The games saved by the Python version are imported once.
