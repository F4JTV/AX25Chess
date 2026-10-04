# Changelog

All notable changes to AX25Chess. Versions follow `CMakeLists.txt`;
`bump_version.sh` updates this file, the Debian changelog and the project
version together.

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
