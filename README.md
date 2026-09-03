# AX25Chess

Point-to-point chess over **AX.25 packet radio**, using **Direwolf** as the
software TNC. Two stations, one game, no server in between.

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

---

## The CHS-1 protocol

Transport: **AX.25 UI frames** (control `0x03`, PID `0xF0`), connectionless,
sent through Direwolf's KISS TCP interface. The payload is **plain ASCII**, so
it is readable as-is in a Direwolf or AGWPE monitor.

### Frame format

```
CHS1|<gid>|<src>|<dst>|<seq>|<TYPE>|<payload>|<crc>
```

| Field | Format | Purpose |
|---|---|---|
| `CHS1` | literal | protocol signature and version |
| `gid` | 4 hex | game identifier, drawn by the initiator |
| `src` | callsign | sender, optional SSID |
| `dst` | callsign | recipient; `ALL` and `CQ` accepted on receive |
| `seq` | 0..9999 | sender's frame counter |
| `TYPE` | 4 letters | see below |
| `payload` | fields separated by `;` | depends on the type |
| `crc` | 4 hex | CRC16-CCITT (poly `0x1021`, init `0xFFFF`) of everything before it |

The application-level CRC overlaps with the AX.25 FCS for bit errors, but it
additionally guards against truncation, concatenated frames and bad KISS
decoding.

### Encoding a move

```
MOVE|WP5;13;29;-;0;A34F
     │   │  │  │ │  └── CRC16 of the position AFTER the move
     │   │  │  │ └───── half-move number, zero based
     │   │  │  └─────── promotion piece, or `-`
     │   │  └────────── destination square, 1..64
     │   └───────────── origin square, 1..64 (redundant, used as a check)
     └───────────────── unique piece identifier
```

A complete frame on the air:

```
CHS1|3F1A|N0CALL|N0CALL-2|7|MOVE|WP5;13;29;-;0;A34F|725A
```

The station `N0CALL` moves piece `WP5` from square 13 to square 29 — that is
e2-e4 — at half-move 0, and the resulting position hashes to `A34F`.

The origin square is redundant: the receiver already knows where `WP5` stands.
It serves as a cross-check — a mismatch reveals a divergence *before* the move
is applied.

Castling is sent as a plain king move (`WK1` to square 7 or 3); the rook's
travel is derived by the engine on both sides. En passant and captures need no
field at all: they follow from the position.

About 45 bytes of information per move, well under the 256 bytes of an AX.25
information field. One frame per move, even at 1200 baud.

### Frame types

| Type | Reliable | Payload | Purpose |
|---|:---:|---|---|
| `HELLO` | yes | `<nonce8hex>;<version>` | invitation, colour draw |
| `ACPT` | yes | `<nonce8hex>;<colour>` | acceptance, claimed colour |
| `MOVE` | yes | `<UID>;<from>;<to>;<promo>;<ply>;<hash>` | one half-move |
| `ACK` | no | `<acked_seq>;<hash>` | acknowledgement |
| `SREQ` | no | `<local_ply_count>` | resynchronisation request |
| `SYNC` | no | `<n>/<total>;<count>;<list>;<gid>` | full history, in blocks |
| `RSGN` | yes | — | resignation |
| `DRWO` / `DRWA` / `DRWD` | yes | — | draw offer / accept / decline |
| `CHAT` | yes | free text | messaging |
| `PING` / `PONG` | no | token | link test |

"Reliable" means an acknowledgement is expected and the frame is retransmitted
until it arrives. Only one reliable frame is in flight at a time.

### Compact history

Each move fits in `<UID>><square>`, with an optional `=Q` suffix:

```
WP5>29,BP5>37,WN2>22,BN1>43,WB2>34,BP1>41,WB2>43,BP4>43
```

Split into blocks of 16 moves to stay under the information-field limit.

### State machine

```
   IDLE ──invite()──▶ HANDSHAKE ──ACPT received──▶ PLAYING ──mate/draw/RSGN──▶ OVER
     ▲                    │                          │
     └────HELLO received──┘                          └──SREQ/SYNC──▶ PLAYING
```

### Receive rules

1. Bad CRC or unknown format → frame silently ignored.
2. `dst` is neither our callsign nor `ALL`/`CQ` → ignored.
3. `src` is not the expected peer → ignored, with a warning.
4. `gid` differs from the current game → ignored, except for `HELLO`.
5. Reliable frame already seen (same `seq`, same type) → **acknowledged again
   without replaying**.
6. `MOVE` with a lower ply than expected → duplicate, acknowledged again.
7. `MOVE` with a higher ply than expected → gap, triggers `SREQ`.
8. Illegal move, inconsistent origin square, or fingerprint mismatch → move
   rolled back, `SREQ` sent.

### Timing

| Parameter | Default | Note |
|---|---|---|
| Retransmission delay | 14 s | adjustable from 5 to 120 s in the RADIO tab |
| Random jitter | 0 to 3 s | avoids retransmission collisions |
| Attempts | 6 | then the operator is warned and the interval quadruples |
| `SYNC` blocks | 16 moves | about 180 bytes per frame |

At 1200 baud VHF a full `MOVE` occupies roughly 45 bytes of information, so a
single frame and about half a second on the air including TXDELAY. The default
delay leaves ample room for the ACK to come back.

---

## Configuring Direwolf

A fully commented file ships as `docs/direwolf.conf.exemple`. The essentials:

```
# Audio device. List what is available with:  direwolf -p
ADEVICE  plughw:1,0        # Windows: a device number, e.g.  ADEVICE 0
CHANNEL  0

MYCALL   N0CALL-7        # your own callsign here
MODEM    1200

# Pick ONE PTT line to match your interface.
#PTT     /dev/ttyUSB0 RTS          # serial RTS
#PTT     COM3 RTS                  # the same, on Windows
#PTT     GPIO 25                   # Raspberry Pi
PTT      RIG 2 localhost:4532      # via rigctld (Hamlib)

# Transmit margins. Lengthen if your radio is slow to come up to power.
TXDELAY  30
TXTAIL   10

# The KISS interface AX25Chess connects to. Without this line Direwolf
# starts but nothing can attach to it.
KISSPORT 8001
```

Then run:

```
direwolf -t 0 -c direwolf.conf
```

`-t 0` disables ANSI colour codes, which matters only when the output is being
captured into a log.

### Points worth knowing

**`KISSPORT` is not optional.** It is the single most common reason the link
fails to come up. Direwolf will start happily without it and simply refuse
every connection. The port number here and the one in the RADIO tab must
match.

**AX25Chess opens exactly one KISS connection.** Direwolf allows three
(`MAX_NET_CLIENTS` in `kiss_frame.h`), and any throwaway probe socket counts
as a full client — it shows up as *Attached to KISS TCP client application N*
followed by *has gone away*. The expected log at start-up is:

```
Ready to accept KISS TCP client application 0 on port 8001 ...
Attached to KISS TCP client application 0 on port 8001 ...
Ready to accept KISS TCP client application 1 on port 8001 ...
```

One `Attached`, no `has gone away`.

**AGW port 8000 is unused.** AX25Chess speaks KISS only; leaving the AGW
listener enabled is harmless.

**Do not run two Direwolf instances on one sound card.** They will fight over
the audio device and neither will work. If a KISS server already answers on
the configured port, AX25Chess connects to it instead of starting another one.

**No digipeater path is needed** for a point-to-point game. If you do route
through a digipeater, enter it in the RADIO tab; each relay adds delay, so
raise the retransmission timeout accordingly.

**On Windows, Direwolf's console appears empty when captured.** The Microsoft
C runtime switches `stdout` to block buffering as soon as it is a pipe rather
than a real console, and Direwolf does not emit the 4 KB needed to flush it.
This does not affect the link. Tick *Separate Direwolf console window* in the
RADIO tab to see the real console.
