"""
i18n.py - Bilinguisme francais / anglais.

Pourquoi pas Qt Linguist. Le circuit habituel (.ts puis .qm) suppose
`lrelease`, que PyQt6 ne fournit pas et qui n'est donc pas garanti sur la
machine de construction. Un catalogue Python integre voyage avec le code,
n'ajoute aucune etape au build, se retrouve tel quel dans le paquet
PyInstaller, et se relit dans une revue de code. Pour trois cents chaines,
c'est le bon compromis ; au-dela de quelques langues il faudrait revenir a
Qt Linguist.

Les chaines sources sont en francais, comme le code. L'anglais est la
traduction. Une entree absente du catalogue retombe donc sur le francais
plutot que sur une cle technique illisible.

Utilisation :

    from .i18n import tr
    label = tr("Lancer une partie")
    texte = tr("Partie {gid} reprise", gid=session.gid)

Les champs sont NOMMES et non positionnels : une traduction anglaise peut
ainsi reordonner la phrase sans que les valeurs se retrouvent inversees.
"""

from __future__ import annotations

from typing import Callable, Optional

LANGUAGES = {"fr": "Francais", "en": "English"}
DEFAULT_LANGUAGE = "fr"

_current = DEFAULT_LANGUAGE
_listeners: list[Callable[[str], None]] = []


# --------------------------------------------------------------------------
# Catalogue
# --------------------------------------------------------------------------
# Genere et maintenu a la main. Clef : la chaine francaise exacte du code.

CATALOG: dict[str, dict[str, str]] = {
    "en": {
        'Theme':
            'Theme',
        'Sombre':
            'Dark',
        'Clair':
            'Light',
        'Le theme clair est concu pour un ecran en plein soleil : contrastes renforces et accent plus fonce.':
            'The light theme is meant for a screen in direct sunlight: stronger contrast and a darker accent.',
        'Premier lancement : utilisation du Direwolf installe avec AX25Chess':
            'First run: using the Direwolf installed with AX25Chess',
        '  (en cours)':
            '  (current)',
        '* DIREWOLF':
            '* DIREWOLF',
        '* LIAISON':
            '* LINK',
        '* TRAIT':
            '* TURN',
        ', dont {waiting} en attente de votre coup':
            ', {waiting} waiting for your move',
        'A vous de jouer : cliquez une piece puis sa case.':
            'Your move: click a piece, then its square.',
        'Abandonner':
            'Resign',
        'Activite':
            'Activity',
        'Arreter Direwolf':
            'Stop Direwolf',
        'Arreter Direwolf en quittant':
            'Stop Direwolf on exit',
        'Aucun acquittement pour {type} apres {n} tentatives - liaison perdue ?':
            'No acknowledgement for {type} after {n} attempts - link lost?',
        "Aucune partie en cours. Les parties sont enregistrees automatiquement apres chaque demi-coup et effacees des qu'elles se terminent.":
            'No saved games. Games are stored automatically after every half-move and removed as soon as they end.',
        'Avancement':
            'Progress',
        'Balise de test':
            'Test beacon',
        'Blancs':
            'White',
        'Case':
            'Square',
        "Ce n'est pas votre trait.":
            'It is not your turn.',
        'Cette partie est celle en cours.':
            'This is the game currently open.',
        'Cette partie est terminee : {label}':
            'This game is over: {label}',
        'Chemin':
            'Path',
        'Choisissez la piece promue :':
            'Choose the promoted piece:',
        'Commande':
            'Command',
        'Configuration':
            'Configuration',
        'Configuration de depart non ecrite : {exc}':
            'Starter configuration not written: {exc}',
        'Configurez la radio puis lancez une invitation.':
            'Set up the radio, then send an invitation.',
        "Confirmez-vous l'abandon ?":
            'Do you confirm your resignation?',
        "Conflit d'attribution des couleurs - relancez l'invitation":
            'Colour assignment conflict - send a new invitation',
        "Connectez-vous d'abord a Direwolf.":
            'Connect to Direwolf first.',
        'Connexion a {host}:{port}...':
            'Connecting to {host}:{port}...',
        'Correspondant':
            'Peer',
        'Couleurs':
            'Colours',
        'Coup':
            'Move',
        "Coup emis, en attente de l'acquittement radio.":
            'Move sent, waiting for the radio acknowledgement.',
        'Coup illegal : {uid} vers la case {case}':
            'Illegal move: {uid} to square {case}',
        'Coup illegal recu : {uid} case {case} - resynchronisation':
            'Illegal move received: {uid} square {case} - resynchronising',
        'Coup precedent non acquitte, patientez.':
            'Previous move not acknowledged, please wait.',
        "Coup recu alors que c'est votre trait - desync":
            'Move received while it is your turn - out of sync',
        'Coup recu hors partie - ignore':
            'Move received outside a game - ignored',
        'Coup recu illisible':
            'Received move could not be read',
        'Coup {ply} deja connu - reacquitte':
            'Move {ply} already known - acknowledged again',
        'Creer':
            'Create',
        'DIREWOLF':
            'DIREWOLF',
        'Delai avant retransmission':
            'Retransmission delay',
        'Demande de resynchronisation envoyee':
            'Resynchronisation requested',
        'Demarrer Direwolf':
            'Start Direwolf',
        'Direwolf : {message}':
            'Direwolf: {message}',
        'Direwolf connecte':
            'Direwolf connected',
        'Direwolf deconnecte':
            'Direwolf disconnected',
        'Direwolf demarre, attente du port KISS...':
            'Direwolf started, waiting for the KISS port...',
        "Direwolf est deja en cours d'execution":
            'Direwolf is already running',
        "Direwolf est deja lance par l'application":
            'Direwolf is already started by the application',
        'Direwolf est pret':
            'Direwolf is ready',
        "Direwolf n'a pas pu demarrer (chemin ou droits d'execution)":
            'Direwolf could not start (path or execute permission)',
        "Direwolf n'a pas pu etre lance dans une fenetre separee : {exc}. Decochez l'option pour revenir a la console integree.":
            'Direwolf could not be started in a separate window: {exc}. Clear the option to return to the embedded console.',
        "Direwolf s'est arrete (code {code})":
            'Direwolf stopped (exit code {code})',
        "Direwolf s'est arrete anormalement":
            'Direwolf terminated abnormally',
        "Direwolf tourne mais rien n'ecoute sur {host}:{port} apres 60 s. Verifiez la ligne `KISSPORT` de direwolf.conf.":
            'Direwolf is running but nothing is listening on {host}:{port} after 60 s. Check the `KISSPORT` line in direwolf.conf.',
        "Direwolf tourne mais rien n'ecoute sur {host}:{port} apres {delay} s. Verifiez la ligne `KISSPORT` de direwolf.conf.":
            'Direwolf is running but nothing is listening on {host}:{port} after {delay} s. Check the `KISSPORT` line in direwolf.conf.',
        'Divergence irreconciliable des historiques - il faut relancer une partie':
            'Irreconcilable move histories - a new game must be started',
        'Echec et mat - les Blancs gagnent':
            'Checkmate - White wins',
        'Echec et mat - les Noirs gagnent':
            'Checkmate - Black wins',
        'Ecriture impossible :\n{exc}':
            'Could not write:\n{exc}',
        "Editez {conf} pour votre peripherique audio et votre commande PTT avant d'emettre":
            'Edit {conf} for your audio device and PTT command before transmitting',
        'Effacer':
            'Clear',
        'Emettre':
            'Send',
        'Empreinte divergente ({mine} != {theirs}) - resync':
            'Fingerprint mismatch ({mine} != {theirs}) - resynchronising',
        'Empreinte du correspondant differente ({theirs} != {mine})':
            'Peer fingerprint differs ({theirs} != {mine})',
        'En attente du coup de votre correspondant.':
            "Waiting for your peer's move.",
        'Etat de la partie':
            'Game status',
        'Executable':
            'Executable',
        'Executable Direwolf':
            'Direwolf executable',
        'Executable introuvable : {exe}':
            'Executable not found: {exe}',
        'Exporter le journal':
            'Export the log',
        'Fenetre console Direwolf separee':
            'Separate Direwolf console window',
        'Fermer':
            'Close',
        'Fichier de configuration Direwolf':
            'Direwolf configuration file',
        'Fichier de configuration introuvable : {conf}':
            'Configuration file not found: {conf}',
        'Fin de partie : {text}':
            'Game over: {text}',
        "Historique recu invalide au coup '{token}' - resynchronisation impossible":
            "Received history invalid at move '{token}' - cannot resynchronise",
        'Hote':
            'Host',
        'Identifiant':
            'Identifier',
        'Identifiants des pieces':
            'Piece identifiers',
        "Indiquez l'indicatif de votre correspondant dans l'onglet RADIO.":
            "Enter your peer's callsign in the RADIO tab.",
        "Indiquez le chemin de l'executable Direwolf.":
            'Enter the path to the Direwolf executable.',
        'Invitation emise, en attente de la reponse.':
            'Invitation sent, waiting for a reply.',
        'Invitation envoyee (partie {gid})':
            'Invitation sent (game {gid})',
        'Journal enregistre : {path}':
            'Log saved: {path}',
        "L'historique de la partie {gid} est incoherent et ne peut pas etre rejoue. Vous pouvez la supprimer depuis le gestionnaire de parties.":
            'The history of game {gid} is inconsistent and cannot be replayed. You can delete it from the game manager.',
        "Lancer Direwolf au demarrage de l'application":
            'Start Direwolf with the application',
        'Lancer une partie':
            'Start a game',
        'Langue':
            'Language',
        'Le correspondant demande une resynchronisation':
            'The peer is requesting a resynchronisation',
        'Liaison KISS TCP':
            'KISS TCP link',
        'Liaison KISS fermee':
            'KISS link closed',
        'MESSAGES':
            'MESSAGES',
        'MESSAGES *':
            'MESSAGES *',
        'Noirs':
            'Black',
        'Nulle par accord mutuel':
            'Draw by mutual agreement',
        'Nulle par la regle des 50 coups':
            'Draw by the fifty-move rule',
        'Nulle par materiel insuffisant':
            'Draw by insufficient material',
        'Nulle par triple repetition':
            'Draw by threefold repetition',
        'Numeros de case':
            'Square numbers',
        'N°':
            'No.',
        'PARTIE':
            'GAME',
        'PRISES BLANCHES':
            'WHITE CAPTURED',
        'PRISES NOIRES':
            'BLACK CAPTURED',
        'Parcourir...':
            'Browse...',
        'Partie':
            'Game',
        'Partie en cours':
            'Game in progress',
        'Partie {gid} : historique incoherent au coup « {token} », reprise abandonnee':
            'Game {gid}: inconsistent history at move « {token} », resume abandoned',
        'Partie {gid} acceptee - vous jouez les {colour}':
            'Game {gid} accepted - you play {colour}',
        'Partie {gid} contre {peer} reprise sur {plies} demi-coups - {trait}':
            'Game {gid} against {peer} resumed at {plies} half-moves - {trait}',
        'Partie {gid} engagee - vous jouez les {colour}':
            'Game {gid} started - you play {colour}',
        'Parties en cours':
            'Saved games',
        'Parties en cours ({count})':
            'Saved games ({count})',
        'Pat - partie nulle':
            'Stalemate - the game is a draw',
        "Pieces {side} capturees, avec leur identifiant. L'ecart affiche a droite est l'avantage materiel des {winner}.":
            "{side} pieces captured, with their identifiers. The figure on the right is {winner}'s material advantage.",
        'Port KISS':
            'KISS port',
        'Programme Direwolf':
            'Direwolf program',
        'Promotion':
            'Promotion',
        'Proposer nulle':
            'Offer draw',
        'Proposition de nulle declinee':
            'Draw offer declined',
        'Proposition de nulle envoyee':
            'Draw offer sent',
        'Proposition de nulle refusee':
            'Draw offer rejected',
        'RADIO':
            'RADIO',
        'Reponse balise recue':
            'Beacon reply received',
        'Reprendre cette partie':
            'Resume this game',
        'Resynchronisation reussie sur {plies} demi-coups (empreinte {hash})':
            'Resynchronised on {plies} half-moves (fingerprint {hash})',
        'Resynchronisation {done}/{total}':
            'Resynchronising {done}/{total}',
        'Resynchroniser':
            'Resynchronise',
        "Retourner l'echiquier":
            'Flip the board',
        'Retransmission {type} seq={seq} (tentative {n}/{total})':
            'Retransmitting {type} seq={seq} (attempt {n}/{total})',
        'Se connecter a Direwolf':
            'Connect to Direwolf',
        'Se connecter automatiquement au port KISS':
            'Connect to the KISS port automatically',
        'Se deconnecter':
            'Disconnect',
        'Situation':
            'Situation',
        'Station':
            'Station',
        'Supprimer':
            'Delete',
        'Supprimer definitivement la partie {gid} contre {peer} ({plies} demi-coups) ?':
            'Permanently delete game {gid} against {peer} ({plies} half-moves)?',
        'TRAMES':
            'FRAMES',
        'Temporisation':
            'Timing',
        'Tous les fichiers (*)':
            'All files (*)',
        'Trait':
            'To move',
        "Trame d'une autre partie ({gid}) ignoree":
            'Frame from another game ({gid}) ignored',
        'Trame ignoree (CRC ou format invalide)':
            'Frame ignored (bad CRC or malformed)',
        'Trame recue de {src}, correspondant attendu {peer} - ignoree':
            'Frame received from {src}, expected peer {peer} - ignored',
        'Trou dans la sequence (recu {got}, attendu {want})':
            'Gap in the sequence (received {got}, expected {want})',
        'Type de trame inconnu : {type}':
            'Unknown frame type: {type}',
        'Un serveur KISS repond deja sur {host}:{port} : Direwolf ne sera pas relance':
            'A KISS server already answers on {host}:{port}: Direwolf will not be started',
        'Une partie contre {peer} est en cours. Elle reste enregistree, mais sera fermee ici. Continuer ?':
            'A game against {peer} is in progress. It stays saved, but will be closed here. Continue?',
        'Vos couleurs':
            'Your colours',
        'Votre correspondant propose la nulle. Acceptez-vous ?':
            'Your peer offers a draw. Do you accept?',
        'Votre indicatif':
            'Your callsign',
        "[AX25Chess] Aucune sortie recue de Direwolf pour l'instant.":
            '[AX25Chess] No output received from Direwolf yet.',
        '[AX25Chess] Aucune sortie recue de Direwolf. Sous Windows, stdout passe en tampon de bloc quand il est capture : les lignes restent bloquees tant que 4 Ko ne sont pas accumules.':
            '[AX25Chess] No output received from Direwolf. On Windows, stdout switches to block buffering once captured: lines stay stuck until 4 KB have accumulated.',
        "[AX25Chess] Cochez « Fenetre console Direwolf separee » dans l'onglet RADIO pour voir la console reelle. Cela ne gene en rien la liaison KISS, qui est detectee par sondage du port.":
            '[AX25Chess] Tick « Separate Direwolf console window » in the RADIO tab to see the real console. This does not affect the KISS link, whose readiness comes from the port.',
        '[AX25Chess] Direwolf arrete (PID {pid}).':
            '[AX25Chess] Direwolf stopped (PID {pid}).',
        "[AX25Chess] Direwolf tourne dans sa propre fenetre console (PID {pid}). Sa sortie s'affiche la-bas, pas ici.":
            '[AX25Chess] Direwolf is running in its own console window (PID {pid}). Its output appears there, not here.',
        '[AX25Chess] Port KISS {host}:{port} ouvert, Direwolf est pret.':
            '[AX25Chess] KISS port {host}:{port} is open, Direwolf is ready.',
        'a vous':
            'you',
        'a vous de jouer':
            'your move',
        'attente ACK {type} seq={seq} ({n}/6)':
            'awaiting ACK {type} seq={seq} ({n}/6)',
        'au correspondant de jouer':
            'peer to move',
        'blanches':
            'white',
        'case {num} ({name}){tag}':
            'square {num} ({name}){tag}',
        'correspondant':
            'peer',
        'coup {n} / {plies} demi-coups':
            'move {n} / {plies} half-moves',
        'empreinte {hash}':
            'fingerprint {hash}',
        'hors partie':
            'no game',
        'message a votre correspondant':
            'message to your peer',
        'negociation des couleurs...':
            'negotiating colours...',
        'noires':
            'black',
        'relais separes par une virgule, ex. WIDE1-1':
            'digipeaters separated by commas, e.g. WIDE1-1',
        'trait aux {trait} | coup {move} | {plies} demi-coups':
            '{trait} to move | move {move} | {plies} half-moves',
        '{call} abandonne':
            '{call} resigns',
        '{call} joue {san}  [{uid} -> case {case}]':
            '{call} plays {san}  [{uid} -> square {case}]',
        "{count} partie(s) en cours{detail} - bouton « Parties en cours » de l'onglet PARTIE":
            '{count} saved game(s){detail} - see the « Saved games » button in the GAME tab',
        '{count} partie(s) enregistree(s){detail}. Double-cliquez sur une ligne pour la reprendre.':
            '{count} saved game(s){detail}. Double-click a row to resume it.',
        '{src} abandonne - vous gagnez':
            '{src} resigns - you win',
        '{src} joue {san}  [{uid} -> case {case}]':
            '{src} plays {san}  [{uid} -> square {case}]',
    },
}


# --------------------------------------------------------------------------
# API
# --------------------------------------------------------------------------

def tr(source: str, /, **fields) -> str:
    """Traduit `source` dans la langue courante et y insere `fields`.

    Le premier parametre est positionnel UNIQUEMENT, d'ou la barre oblique.
    Sans elle, un champ portant le meme nom que lui — et le catalogue emploie
    bien un champ « text » — provoquerait un TypeError au moment precis ou la
    chaine doit s'afficher, c'est-a-dire en pleine partie. La barre oblique
    reserve tous les noms de champs a l'operateur.

    Une chaine sans traduction est rendue telle quelle : l'interface reste
    lisible pendant qu'une traduction manque, au lieu d'afficher un vide.
    """
    out = CATALOG.get(_current, {}).get(source, source)
    if fields:
        try:
            out = out.format(**fields)
        except (KeyError, IndexError, ValueError):
            # Traduction mal formee : on retombe sur la source plutot que de
            # faire echouer l'affichage.
            try:
                out = source.format(**fields)
            except Exception:
                pass
    return out


def set_language(code: str) -> None:
    global _current
    code = (code or DEFAULT_LANGUAGE).lower()[:2]
    if code not in LANGUAGES:
        code = DEFAULT_LANGUAGE
    if code == _current:
        return
    _current = code
    for listener in list(_listeners):
        listener(code)


def current_language() -> str:
    return _current


def language_name(code: Optional[str] = None) -> str:
    return LANGUAGES.get(code or _current, LANGUAGES[DEFAULT_LANGUAGE])


def on_language_changed(callback: Callable[[str], None]) -> None:
    """Enregistre un rappel appele a chaque changement de langue."""
    if callback not in _listeners:
        _listeners.append(callback)


def detect_language() -> str:
    """Langue du systeme, limitee a celles que nous savons parler.

    AX25CHESS_LANG force le choix. Utile pour les bancs de test, dont les
    assertions sur des libelles doivent etre deterministes, et pour un
    operateur dont la locale ne correspond pas a la langue voulue.
    """
    import os
    forced = os.environ.get("AX25CHESS_LANG", "").strip().lower()[:2]
    if forced in LANGUAGES:
        return forced
    try:
        from PyQt6.QtCore import QLocale
        code = QLocale.system().name().split("_")[0].lower()
    except Exception:
        import locale
        code = (locale.getdefaultlocale()[0] or "fr").split("_")[0].lower()
    return code if code in LANGUAGES else "en"


def missing_entries() -> list[str]:
    """Chaines sources sans traduction anglaise. Utilisee par les tests."""
    return sorted(set(SOURCES) - set(CATALOG.get("en", {})))


# Chaines traduites au travers d'une variable plutot que par un appel
# litteral a tr() : les noms de themes passent par tr(THEMES[code]). Elles
# sont declarees ici pour que le controle de couverture ne les prenne pas pour
# des entrees orphelines.
DYNAMIC_SOURCES = ("Sombre", "Clair")

# Chaines sources connues, deduites du catalogue lui-meme.
SOURCES: list[str] = sorted(CATALOG["en"])
