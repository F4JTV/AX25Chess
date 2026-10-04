#!/usr/bin/env python3
"""List the strings the sources hand to tr(), qsTr() or translate().

Adjacent C++ literals are joined, as the compiler joins them.  With
--missing, only those absent from src/app/catalog_fr.inc are printed:
the list to translate after a change of text.  tests/tst_ui.cpp checks the
same thing at build time.

This file is part of AX25Chess.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIT = r'"(?:[^"\\]|\\.)*"'
CALL = re.compile(r'(?:\bqsTr|\btr|translate\(\s*"[^"]*"\s*,)\s*\(?\s*((?:' + LIT + r'\s*)+)')

def unescape(s):
    return s.replace('\\"', '"').replace("\\n", "\n").replace("\\u2022", "\u2022").replace("\\\\", "\\")

def strings():
    out = []
    for d in ("src/app", "src/qml", "src/chess", "src/radio", "src/engine"):
        for f in sorted((ROOT / d).glob("*")):
            if f.suffix not in (".cpp", ".h", ".qml"):
                continue
            for m in CALL.finditer(f.read_text(encoding="utf-8")):
                s = unescape("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))))
                if s not in out:
                    out.append(s)
    return out

def catalog():
    text = (ROOT / "src/app/catalog_fr.inc").read_text(encoding="utf-8")
    keys = set()
    for m in re.finditer(r'\{\s*QStringLiteral\(((?:\s*' + LIT + r')+)\s*\)\s*,', text):
        keys.add(unescape("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))))
    return keys

if __name__ == "__main__":
    have = catalog() if "--missing" in sys.argv else set()
    for s in strings():
        if s not in have:
            print(repr(s))
