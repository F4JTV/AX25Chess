#!/usr/bin/env python3
"""
kiss_hub.py - Concentrateur KISS pour essais sans radio.

Simule un canal partage : tout ce qu'un client emet est repete a tous les
autres. Permet de faire jouer deux instances d'AX25Chess sur le meme PC, ou
sur deux PC du reseau local, avant de passer sur l'air.

    python3 tools/kiss_hub.py --port 8001 --loss 0.2 --delay 0.4

--loss  taux de perte simule (0 a 1)      --delay  latence en secondes
"""

import argparse
import random
import socket
import threading
import time

clients: list[socket.socket] = []
lock = threading.Lock()


def relay(sock, addr, loss, delay):
    with lock:
        clients.append(sock)
    print(f"[hub] {addr} connecte ({len(clients)} station(s))")
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            if random.random() < loss:
                print(f"[hub] {len(data)} o perdus (QSB simule)")
                continue
            if delay:
                time.sleep(delay)
            with lock:
                others = [c for c in clients if c is not sock]
            for c in others:
                try:
                    c.sendall(data)
                except OSError:
                    pass
            print(f"[hub] {addr} -> {len(others)} station(s), {len(data)} o")
    finally:
        with lock:
            if sock in clients:
                clients.remove(sock)
        sock.close()
        print(f"[hub] {addr} deconnecte")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8001)
    ap.add_argument("--loss", type=float, default=0.0)
    ap.add_argument("--delay", type=float, default=0.0)
    a = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((a.host, a.port))
    srv.listen(8)
    print(f"[hub] concentrateur KISS sur {a.host}:{a.port} "
          f"(pertes {a.loss:.0%}, latence {a.delay}s)")
    try:
        while True:
            sock, addr = srv.accept()
            threading.Thread(target=relay, args=(sock, addr, a.loss, a.delay),
                             daemon=True).start()
    except KeyboardInterrupt:
        print("\n[hub] arret")


if __name__ == "__main__":
    main()
