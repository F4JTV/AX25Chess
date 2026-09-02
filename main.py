#!/usr/bin/env python3
"""Point d'entree AX25Chess."""

import sys

from ax25chess import __version__


def main() -> int:
    # Traite avant toute chose : le script de construction lance l'executable
    # gele avec --version pour verifier qu'il resout ses imports, sans ouvrir
    # de fenetre ni toucher aux reglages.
    if any(arg in ("--version", "-V") for arg in sys.argv[1:]):
        print(f"AX25Chess {__version__}")
        return 0

    from PyQt6.QtGui import QIcon
    from PyQt6.QtWidgets import QApplication

    from ax25chess.main_window import MainWindow
    from ax25chess.resources import icon_path

    app = QApplication(sys.argv)
    app.setApplicationName("AX25Chess")
    app.setApplicationVersion(__version__)
    app.setOrganizationName("F4JTV")

    icon = icon_path()
    if icon:
        app.setWindowIcon(QIcon(icon))

    win = MainWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
