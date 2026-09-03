"""
ax25_kiss.py - Encapsulation KISS et trames AX.25 UI pour Direwolf.

Direwolf expose un port TCP KISS (par defaut 8001). On y envoie des trames
AX.25 UI (control 0x03, PID 0xF0) en mode non connecte : la fiabilite est
assuree au niveau applicatif par le protocole CHS (voir protocol.py).

Format KISS
    FEND(0xC0) | cmd(port<<4 | 0x00) | trame echappee | FEND
    echappement : 0xC0 -> 0xDB 0xDC   ;   0xDB -> 0xDB 0xDD

Format AX.25 UI
    adresse dest (7 o) | adresse src (7 o) | [digis 7 o]* | 0x03 | 0xF0 | info
    Chaque adresse : 6 caracteres decales de 1 bit a gauche + octet SSID.
    Octet SSID : C/H | 1 | 1 | SSID(4 bits) | bit d'extension
"""

from __future__ import annotations

FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD

UI_CONTROL = 0x03
PID_NO_L3 = 0xF0

MAX_INFO = 236          # marge sous les 256 octets du champ info AX.25


class AX25Error(Exception):
    pass


# --------------------------------------------------------------------------
# Adresses
# --------------------------------------------------------------------------

def split_callsign(text: str) -> tuple[str, int]:
    """'N0CALL-7' -> ('N0CALL', 7)"""
    text = text.strip().upper()
    if "-" in text:
        base, ssid = text.split("-", 1)
        try:
            ssid = int(ssid)
        except ValueError:
            raise AX25Error(f"SSID invalide dans {text}")
    else:
        base, ssid = text, 0
    if not 1 <= len(base) <= 6 or not base.isalnum():
        raise AX25Error(f"indicatif invalide: {text}")
    if not 0 <= ssid <= 15:
        raise AX25Error(f"SSID hors bornes: {text}")
    return base, ssid


def encode_address(call: str, last: bool = False, cbit: bool = False,
                   has_been_repeated: bool = False) -> bytes:
    base, ssid = split_callsign(call)
    out = bytearray()
    for ch in base.ljust(6):
        out.append(ord(ch) << 1)
    b = 0x60 | (ssid << 1)
    if cbit or has_been_repeated:
        b |= 0x80
    if last:
        b |= 0x01
    out.append(b)
    return bytes(out)


def decode_address(raw: bytes) -> tuple[str, bool, bool]:
    """Retourne (indicatif, dernier_de_la_liste, bit_haut)."""
    call = "".join(chr(b >> 1) for b in raw[:6]).strip()
    ssid = (raw[6] >> 1) & 0x0F
    last = bool(raw[6] & 0x01)
    high = bool(raw[6] & 0x80)
    if ssid:
        call = f"{call}-{ssid}"
    return call, last, high


# --------------------------------------------------------------------------
# Trames AX.25 UI
# --------------------------------------------------------------------------

def build_ui_frame(src: str, dst: str, info: bytes,
                   path: list[str] | None = None) -> bytes:
    if len(info) > MAX_INFO:
        raise AX25Error(f"champ info trop long ({len(info)} > {MAX_INFO})")
    path = [p for p in (path or []) if p.strip()]
    if len(path) > 8:
        raise AX25Error("plus de 8 relais dans le chemin")

    frame = bytearray()
    frame += encode_address(dst, last=False, cbit=True)
    frame += encode_address(src, last=not path, cbit=False)
    for i, digi in enumerate(path):
        frame += encode_address(digi, last=(i == len(path) - 1))
    frame.append(UI_CONTROL)
    frame.append(PID_NO_L3)
    frame += info
    return bytes(frame)


def parse_ui_frame(frame: bytes) -> dict | None:
    """Decode une trame AX.25. Retourne None si ce n'est pas une UI exploitable."""
    if len(frame) < 16:
        return None
    addrs = []
    pos = 0
    while pos + 7 <= len(frame):
        call, last, _ = decode_address(frame[pos:pos + 7])
        addrs.append(call)
        pos += 7
        if last:
            break
        if len(addrs) > 10:
            return None
    if len(addrs) < 2 or pos + 2 > len(frame):
        return None
    control = frame[pos]
    pid = frame[pos + 1]
    if control != UI_CONTROL or pid != PID_NO_L3:
        return None
    return {
        "dst": addrs[0],
        "src": addrs[1],
        "path": addrs[2:],
        "info": frame[pos + 2:],
    }


# --------------------------------------------------------------------------
# KISS
# --------------------------------------------------------------------------

def kiss_wrap(frame: bytes, port: int = 0) -> bytes:
    out = bytearray([FEND, (port << 4) & 0xF0])
    for b in frame:
        if b == FEND:
            out += bytes([FESC, TFEND])
        elif b == FESC:
            out += bytes([FESC, TFESC])
        else:
            out.append(b)
    out.append(FEND)
    return bytes(out)


class KissDecoder:
    """Automate d'extraction de trames dans un flux TCP KISS."""

    def __init__(self):
        self._buf = bytearray()
        self._in_frame = False
        self._escaped = False

    def feed(self, data: bytes) -> list[bytes]:
        frames = []
        for b in data:
            if b == FEND:
                if self._in_frame and self._buf:
                    payload = bytes(self._buf)
                    # premier octet = commande KISS ; 0x00 = donnees
                    if (payload[0] & 0x0F) == 0x00 and len(payload) > 1:
                        frames.append(payload[1:])
                self._buf.clear()
                self._in_frame = True
                self._escaped = False
                continue
            if not self._in_frame:
                continue
            if self._escaped:
                self._buf.append(FEND if b == TFEND else FESC if b == TFESC else b)
                self._escaped = False
            elif b == FESC:
                self._escaped = True
            else:
                self._buf.append(b)
            if len(self._buf) > 1024:          # garde-fou anti-flux corrompu
                self._buf.clear()
                self._in_frame = False
        return frames
