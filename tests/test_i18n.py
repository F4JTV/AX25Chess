#!/usr/bin/env python3
"""
Bilinguisme francais / anglais.

Verifie que le catalogue couvre toutes les chaines effectivement passees a
tr() dans le code, que les champs nommes concordent entre source et
traduction, et que la bascule de langue redessine reellement l'interface.
"""

import ast
import os
import re
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

results: list[bool] = []


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


def collect_tr_calls() -> set[str]:
    """Premier argument litteral de chaque appel a tr() du paquet."""
    found = set()
    for path in sorted((ROOT / "ax25chess").glob("*.py")):
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if (isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Name)
                    and node.func.id == "tr"
                    and node.args
                    and isinstance(node.args[0], ast.Constant)
                    and isinstance(node.args[0].value, str)):
                found.add(node.args[0].value)
    return found


def fields(text: str) -> set[str]:
    return set(re.findall(r"\{(\w+)\}", text))


def main() -> int:
    from ax25chess import i18n

    catalog = i18n.CATALOG["en"]
    used = collect_tr_calls()

    check(f"catalogue non vide ({len(catalog)} entrees)", len(catalog) > 100)
    check(f"chaines traduites employees dans le code ({len(used)})", len(used) > 100)

    untranslated = sorted(used - set(catalog))
    check("toute chaine passee a tr() a une traduction", not untranslated)
    for miss in untranslated[:10]:
        print("      manquante :", repr(miss))

    # Les chaines passees a tr() par une variable sont declarees dans i18n :
    # l'analyse syntaxique ne peut pas les voir.
    used |= set(i18n.DYNAMIC_SOURCES)
    unused = sorted(set(catalog) - used)
    check("aucune entree orpheline dans le catalogue", not unused)
    for extra in unused[:10]:
        print("      orpheline :", repr(extra))

    # Les champs nommes doivent concorder, sinon tr() retomberait
    # silencieusement sur le francais a l'execution.
    mismatched = [src for src, dst in catalog.items()
                  if fields(src) != fields(dst)]
    check("champs nommes concordants entre source et traduction",
          not mismatched)
    for bad in mismatched[:10]:
        print("      champs differents :", repr(bad))

    check("aucune traduction vide",
          all(value.strip() for value in catalog.values()))

    # Comportement du moteur
    i18n.set_language("fr")
    check("langue francaise par defaut", i18n.current_language() == "fr")
    check("la source est rendue telle quelle en francais",
          i18n.tr("Lancer une partie") == "Lancer une partie")
    i18n.set_language("en")
    check("traduction anglaise appliquee",
          i18n.tr("Lancer une partie") == "Start a game")
    check("insertion de champs nommes",
          i18n.tr("Coup illegal : {uid} vers la case {case}",
                  uid="WP5", case=29) == "Illegal move: WP5 to square 29")
    check("chaine inconnue rendue sans erreur",
          i18n.tr("Texte absent du catalogue") == "Texte absent du catalogue")
    check("langue inconnue ramenee au francais",
          (i18n.set_language("xx"), i18n.current_language())[1] == "fr")

    os.environ["AX25CHESS_LANG"] = "en"
    check("AX25CHESS_LANG force la langue", i18n.detect_language() == "en")
    os.environ["AX25CHESS_LANG"] = "fr"

    # Bascule a chaud sur la fenetre reelle
    from PyQt6.QtCore import QSettings
    from PyQt6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    app.setQuitOnLastWindowClosed(False)
    settings = QSettings("F4JTV", "AX25Chess")
    settings.clear()
    settings.setValue("language", "fr")
    settings.sync()

    from ax25chess.main_window import MainWindow
    win = MainWindow()

    def snapshot() -> dict:
        return {
            "tabs": [win.tabs.tabText(i) for i in range(win.tabs.count())],
            "invite": win.btn_invite.text(),
            "resign": win.btn_resign.text(),
            "headers": [win.table.horizontalHeaderItem(i).text()
                        for i in range(win.table.columnCount())],
            "hint": win.hint.text(),
            "uid": win.chk_uid.text(),
        }

    french = snapshot()
    check("interface construite en francais",
          french["invite"] == "Lancer une partie"
          and french["tabs"][0] == "PARTIE")

    win.cb_lang.setCurrentIndex(win.cb_lang.findData("en"))
    english = snapshot()
    check("bascule vers l'anglais",
          english["invite"] == "Start a game"
          and english["tabs"][0] == "GAME"
          and english["headers"][0] == "No.")
    check("texte d'aide traduit aussi", english["hint"] != french["hint"])
    check("cases a cocher traduites", english["uid"] != french["uid"])

    win.cb_lang.setCurrentIndex(win.cb_lang.findData("fr"))
    check("retour au francais complet", snapshot() == french)

    win.close()
    print("\nTOUS LES TESTS PASSENT" if all(results) else "\nDES TESTS ONT ECHOUE")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
