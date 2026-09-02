#!/usr/bin/env bash
# Lanceur AX25Chess : cree l'environnement virtuel au premier appel.
set -e
cd "$(dirname "$0")"
if [ ! -d .venv ]; then
    echo "Creation de l'environnement virtuel..."
    python3 -m venv .venv
    ./.venv/bin/pip install --upgrade pip -q
    ./.venv/bin/pip install -r requirements.txt
fi
exec ./.venv/bin/python main.py "$@"
