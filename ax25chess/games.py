"""
games.py - Magasin des parties commencees et non terminees.

Une partie par fichier dans ~/.ax25chess/parties/, nommee d'apres son
identifiant de partie et l'indicatif du correspondant. On peut ainsi mener
plusieurs parties de front — ce qui arrive naturellement en radio, ou une
partie s'etale sur plusieurs jours et plusieurs correspondants.

Le fichier ne contient que la liste des coups en forme compacte : rejouee
depuis la position initiale, elle reconstruit la position, les identifiants de
pieces, les droits de roque, la prise en passant et l'historique des
repetitions. Un instantane FEN perdrait tout cela.
"""

from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

STATE_DIR = Path.home() / ".ax25chess"
GAMES_DIR = STATE_DIR / "parties"
LEGACY_FILE = STATE_DIR / "partie_en_cours.json"

_SAFE = re.compile(r"[^A-Za-z0-9_-]")


@dataclass
class SavedGame:
    gid: str
    my_call: str
    peer_call: str
    color: str
    moves: list[str] = field(default_factory=list)
    nonce: int = 0
    peer_nonce: Optional[int] = None
    seq: int = 0
    created: float = 0.0
    updated: float = 0.0
    path: Optional[Path] = None

    # -- lecture confortable pour l'interface -------------------------------

    @property
    def ply_count(self) -> int:
        return len(self.moves)

    @property
    def move_number(self) -> int:
        return self.ply_count // 2 + 1

    @property
    def side_to_move(self) -> str:
        return "W" if self.ply_count % 2 == 0 else "B"

    @property
    def my_turn(self) -> bool:
        return self.side_to_move == self.color

    def age_text(self) -> str:
        if not self.updated:
            return "-"
        delta = time.time() - self.updated
        if delta < 90:
            return "a l'instant"
        if delta < 3600:
            return f"il y a {int(delta // 60)} min"
        if delta < 86400:
            return f"il y a {int(delta // 3600)} h"
        if delta < 7 * 86400:
            return f"il y a {int(delta // 86400)} j"
        return time.strftime("%d/%m/%Y", time.localtime(self.updated))

    def to_dict(self) -> dict:
        return {
            "gid": self.gid,
            "call": self.my_call,
            "peer": self.peer_call,
            "color": self.color,
            "moves": self.moves,
            "nonce": self.nonce,
            "peer_nonce": self.peer_nonce,
            "seq": self.seq,
            "created": self.created,
            "updated": self.updated,
        }

    @classmethod
    def from_dict(cls, data: dict, path: Optional[Path] = None) -> "SavedGame":
        return cls(
            gid=str(data.get("gid", "0000")),
            my_call=str(data.get("call", "")),
            peer_call=str(data.get("peer", "")),
            color=str(data.get("color") or "W"),
            moves=[str(m) for m in data.get("moves", [])],
            nonce=int(data.get("nonce", 0) or 0),
            peer_nonce=data.get("peer_nonce"),
            seq=int(data.get("seq", 0) or 0),
            created=float(data.get("created", 0) or 0),
            updated=float(data.get("updated", 0) or 0),
            path=path,
        )


class GameStore:
    """Acces au repertoire des parties en cours."""

    def __init__(self, directory: Path = GAMES_DIR):
        self.dir = Path(directory)

    # -- chemins ------------------------------------------------------------

    def _filename(self, gid: str, peer: str) -> Path:
        safe_gid = _SAFE.sub("_", gid or "0000")
        safe_peer = _SAFE.sub("_", (peer or "INCONNU").upper())
        return self.dir / f"{safe_gid}-{safe_peer}.json"

    # -- lecture ------------------------------------------------------------

    def list(self) -> list[SavedGame]:
        """Parties enregistrees, de la plus recente a la plus ancienne."""
        self.migrate_legacy()
        if not self.dir.is_dir():
            return []
        games = []
        for path in self.dir.glob("*.json"):
            game = self._read(path)
            if game is not None:
                games.append(game)
        games.sort(key=lambda g: g.updated, reverse=True)
        return games

    def _read(self, path: Path) -> Optional[SavedGame]:
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            return None
        if not isinstance(data, dict) or not data.get("moves") and not data.get("gid"):
            return None
        game = SavedGame.from_dict(data, path)
        if not game.updated:
            try:
                game.updated = path.stat().st_mtime
            except OSError:
                game.updated = 0.0
        return game

    def find(self, gid: str, peer: str) -> Optional[SavedGame]:
        path = self._filename(gid, peer)
        return self._read(path) if path.is_file() else None

    def count(self) -> int:
        return len(self.list())

    # -- ecriture -----------------------------------------------------------

    def save(self, gid: str, my_call: str, peer_call: str, color: str,
             moves: list[str], nonce: int = 0, peer_nonce: Optional[int] = None,
             seq: int = 0) -> Optional[Path]:
        path = self._filename(gid, peer_call)
        existing = self._read(path) if path.is_file() else None
        now = time.time()
        game = SavedGame(
            gid=gid, my_call=my_call, peer_call=peer_call, color=color or "W",
            moves=list(moves), nonce=nonce, peer_nonce=peer_nonce, seq=seq,
            created=existing.created if existing and existing.created else now,
            updated=now, path=path)
        try:
            self.dir.mkdir(parents=True, exist_ok=True)
            tmp = path.with_suffix(".tmp")
            tmp.write_text(json.dumps(game.to_dict(), indent=1))
            tmp.replace(path)          # ecriture atomique : pas de fichier tronque
        except OSError:
            return None
        return path

    def delete(self, gid: str, peer: str) -> bool:
        path = self._filename(gid, peer)
        try:
            path.unlink()
            return True
        except OSError:
            return False

    def delete_game(self, game: SavedGame) -> bool:
        if game.path is not None:
            try:
                game.path.unlink()
                return True
            except OSError:
                return False
        return self.delete(game.gid, game.peer_call)

    # -- compatibilite ------------------------------------------------------

    def migrate_legacy(self) -> None:
        """Reprend l'ancien fichier unique, puis le supprime."""
        if not LEGACY_FILE.is_file():
            return
        try:
            data = json.loads(LEGACY_FILE.read_text())
        except (OSError, ValueError):
            LEGACY_FILE.unlink(missing_ok=True)
            return
        game = SavedGame.from_dict(data)
        if game.moves:
            self.save(game.gid, game.my_call, game.peer_call, game.color,
                      game.moves, game.nonce, game.peer_nonce, game.seq)
        LEGACY_FILE.unlink(missing_ok=True)
