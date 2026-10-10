# The CHS-1 protocol

Transport: **AX.25 UI frames** (control `0x03`, PID `0xF0`), connectionless,
handed to the modem (Dire Wolf, compiled into the program). The payload is **plain ASCII**, so
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
| Retransmission delay | 14 s | 5 to 120 s, Settings -> Game |
| Random jitter | 0 to 3 s | avoids retransmission collisions |
| Attempts | 6 | then the operator is warned and the interval quadruples |
| `SYNC` blocks | 16 moves | about 180 bytes per frame |

At 1200 baud VHF a full `MOVE` occupies roughly 45 bytes of information, so a
single frame and about half a second on the air including TXDELAY. The default
delay leaves ample room for the ACK to come back.



### Receive-side behaviour added in version 2.0

A `HELLO` whose game identifier differs from the current game opens a new
sequence space: the inviting station starts its frame counter again for
every game, so its `HELLO` usually carries the `seq` of the previous game's
`HELLO`. Version 1.0 took it for a duplicate, acknowledged it and never
answered with `ACPT`, leaving the inviter in the handshake. Version 2.0
clears its duplicate memory on such a `HELLO`. Nothing changes on the air,
and a retransmitted `HELLO` of the same game is still deduplicated.

### Interoperability

The fingerprint is the CRC16 of the position key, a `|`, the half-move
clock, a `|` and the number of half-moves played. The position key is the
64 squares from a1 to h8 (`.` for empty, the piece letter, upper case for
White), then `|`, the side to move (`W` or `B`), `|`, the castling rights in
the order `KQkq`, `|`, and the en passant square as an index 0..63 or `-`.
Text goes on the air in ASCII, any other character replaced by `?`.

`tests/vectors/python_vectors.json` holds games, frames and CRCs produced
by the Python version 1.0 (`tests/vectors/make_vectors.py`); the C++ tests
replay them and compare byte for byte.

### Receive-side behaviour added in version 2.0.5

Nothing changes on the air; these rules only decide how a station answers.

- **A reliable frame is remembered as received only once it is
  acknowledged.** A frame turned down without an `ACK` (a move out of
  sequence, a fingerprint that does not match) is handled again when it is
  repeated. It used to be remembered on arrival, so its repetition passed
  for a duplicate and was acknowledged without ever having been applied:
  the sender believed its move played, the receiver had not played it.
- **A `MOVE` that arrives after the game ended here is acknowledged, not
  played.** The other station did not learn of the end in time (a lost
  `RSGN`, say); without an `ACK` it would repeat the move for ever.
- **A `HELLO` drops whatever the previous game left to send.** A move or a
  message still waiting for its `ACK` would never get one (the peer
  ignores another game's frames) and held the `ACPT` behind it: the
  invitation was never answered.
- **Crossed invitations.** When both stations invite at the same moment,
  each would accept the other's `HELLO` and end up in a different game.
  While its own `HELLO` is still unacknowledged, a station keeps the
  invitation with the smaller game identifier and leaves the other `HELLO`
  unanswered; the other station, on receiving the first `HELLO`, accepts it.
  Once a station's `HELLO` has been acknowledged, a new `HELLO` from the
  peer is a deliberate new invitation and is accepted.
