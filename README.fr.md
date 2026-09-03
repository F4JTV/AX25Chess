# AX25Chess

Jeu d'échecs point à point par radio packet **AX.25**, via **Direwolf** (KISS TCP).
Interface **PyQt6**. Aucune dépendance hors PyQt6 : le moteur d'échecs, la pile
AX.25/KISS et le protocole applicatif sont écrits pour ce projet.

---

## 1. Le principe demandé

Chaque pièce porte un **identifiant unique et stable** pour toute la partie, et
chaque case porte un **numéro unique de 1 à 64**. Un coup transmis sur l'air est
donc exactement le triplet demandé : **indicatif + identifiant de pièce + case**.

### Identifiants de pièces (32 UID)

```
WR1 WN1 WB1 WQ1 WK1 WB2 WN2 WR2      BR1 BN1 BB1 BQ1 BK1 BB2 BN2 BR2
WP1 .. WP8                            BP1 .. BP8
```

Une promotion **conserve** l'UID : `WP5` promu dame reste `WP5`, seul son type
change. L'unicité et la traçabilité restent absolues du premier au dernier coup.

### Numérotation des cases

`numéro = (rang - 1) × 8 + colonne + 1`, donc **a1 = 1**, **h1 = 8**, **h8 = 64**.

```
  8 | 57 58 59 60 61 62 63 64
  7 | 49 50 51 52 53 54 55 56
  ...
  1 |  1  2  3  4  5  6  7  8
      a  b  c  d  e  f  g  h
```

---

## 2. Protocole radio « CHS-1 »

Trames **AX.25 UI** (control `0x03`, PID `0xF0`), non connectées, texte ASCII pur —
lisibles telles quelles dans un moniteur Direwolf ou AGWPE.

```
CHS1|<gid>|<src>|<dst>|<seq>|<TYPE>|<payload>|<crc16>
```

Un coup :

```
CHS1|3F1A|N0CALL|N0CALL-2|7|MOVE|WP5;13;29;-;0;A34F|725A
                                │   │  │  │ │  └─ empreinte de la position résultante
                                │   │  │  │ └──── numéro de demi-coup
                                │   │  │  └────── promotion (- si aucune)
                                │   │  └───────── case d'arrivée  (29 = e4)
                                │   └──────────── case de départ   (13 = e2)
                                └──────────────── identifiant de la pièce
```

| Type | Rôle |
|---|---|
| `HELLO` / `ACPT` | invitation, tirage des couleurs |
| `MOVE` / `ACK` | coup et acquittement |
| `SREQ` / `SYNC` | demande et envoi de l'historique complet |
| `RSGN`, `DRWO`, `DRWA`, `DRWD` | abandon, proposition / acceptation / refus de nulle |
| `CHAT`, `PING`, `PONG` | messagerie et balise de test |

Taille d'un `MOVE` complet : environ 45 octets d'information, très en dessous des
256 octets du champ info AX.25 — une seule trame par coup, même en 1200 bauds.

---

## 3. La logique qui rend la partie fiable

C'est le vrai sujet : le canal est lent, semi‑duplex et sans garantie. Sept
mécanismes se combinent.

**1. Stop‑and‑wait naturel.** Seul le joueur au trait a le droit d'émettre un
coup, et il n'y a jamais plus d'une trame fiable en vol. Les échecs sont un jeu
alterné : la structure du jeu *est* le contrôle de flux. Aucune fenêtre glissante,
aucune collision entre deux coups.

**2. Numérotation par demi‑coup.** Chaque `MOVE` porte son index de ply. Le
receveur n'accepte que `ply == attendu` : un ply inférieur est un doublon (on
ré‑acquitte sans rejouer), un ply supérieur est un trou (on demande une resync).

**3. Empreinte de position dans chaque coup.** L'émetteur joue le coup chez lui et
joint le CRC16 de la position obtenue. Le receveur applique le coup et compare.
**Une divergence est détectée au demi‑coup près**, pas dix coups plus tard quand
plus rien n'est rattrapable. C'est le mécanisme le plus important de tout le
protocole.

**4. Double validation, aucun arbitre.** Les deux stations font tourner le même
moteur de règles. Un coup reçu est vérifié contre la liste des coups légaux
locaux avant d'être appliqué. Les échecs étant déterministes, il n'y a pas besoin
d'une station maîtresse : les deux états ne peuvent pas diverger silencieusement.

**5. Acquittement et retransmission temporisée.** Retransmission toutes les 14 s
(réglable) avec gigue aléatoire pour éviter les collisions, 6 tentatives, puis
alerte à l'opérateur. Un doublon reçu est toujours ré‑acquitté : l'idempotence
évite l'enlisement quand c'est l'ACK qui a été perdu.

**6. Resynchronisation par rejeu de l'historique, pas par instantané.** `SYNC`
transporte la liste complète des coups en forme compacte (`WP5>29`), découpée en
blocs de 16 pour tenir dans le champ info. Le receveur rejoue tout depuis la
position initiale : cela reconstruit non seulement la position, mais aussi les
UID, les droits de roque, la prise en passant et l'historique des répétitions —
ce qu'un simple FEN perdrait. L'historique local doit être un **préfixe** de
celui reçu, sinon la divergence est déclarée irréconciliable plutôt que masquée.

**7. Tirage des couleurs sans arbitre.** Chaque station tire un nonce 32 bits dans
`HELLO`/`ACPT` ; le plus grand nonce prend les Blancs, égalité tranchée par
l'ordre alphabétique des indicatifs. Les deux stations arrivent à la même
conclusion sans négociation supplémentaire.

**En complément :** sauvegarde automatique après chaque demi‑coup — une partie
survit à un QRT, une coupure ou un plantage. Voir la section suivante.

---

## Parties en cours

En radio, une partie s'étale souvent sur plusieurs jours et plusieurs
correspondants. Chaque partie est donc enregistrée dans son propre fichier sous
`~/.ax25chess/parties/`, nommé d'après son identifiant et l'indicatif du
correspondant. Le bouton **Parties en cours (N)** de l'onglet PARTIE ouvre la
liste : identifiant, correspondant, vos couleurs, avancement, à qui est le
trait, dernière activité. On y reprend une partie d'un double‑clic, ou on la
supprime.

Le fichier ne contient que la liste des coups en forme compacte. Rejouée depuis
la position initiale, elle reconstruit la position, les identifiants de pièces,
les droits de roque, la prise en passant et l'historique des répétitions — ce
qu'un instantané FEN perdrait. Un historique incohérent est refusé proprement
plutôt que chargé à moitié.

L'écriture est atomique (fichier temporaire puis renommage) : une coupure de
courant pendant la sauvegarde ne laisse jamais un fichier tronqué. Une partie
est effacée dès qu'elle se termine, et une partie reprise déjà terminée (mat
reçu avant la fermeture) est détectée puis nettoyée.

Rien n'est imposé au démarrage : l'application signale simplement le nombre de
parties en attente dans le journal et sur le bouton. L'ancien fichier unique
`partie_en_cours.json` est repris automatiquement s'il existe, puis supprimé.

---

## 4. Installation

### Depuis les sources

```bash
./lancer.sh                 # cree le venv au premier appel puis demarre
```

ou manuellement :

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python3 main.py
```

### Construire l'installateur Windows

Il faut un Python 3.10 ou superieur **en 64 bits**, et Inno Setup 6.3 ou
superieur pour l'installateur.

```powershell
.\build_windows.ps1              # build complet
.\build_windows.ps1 app          # build portable seul, sans installateur
.\build_windows.ps1 installer    # installateur seul, depuis dist\
.\build_windows.ps1 clean        # nettoie puis reconstruit
```

Le script cree un `.venv-build` local, y installe PyQt6, PyInstaller et
Pillow, genere l'icone et les fichiers de version, lance PyInstaller, verifie
que l'executable produit demarre, puis compile l'installateur.
`-InnoSetupPath` force l'emplacement d'ISCC si besoin.

**La recherche d'Inno Setup ne suppose aucun chemin.** Inno Setup 7 existe en
32 et en 64 bits, donc sous « Program Files » comme sous « Program Files
(x86) », et il peut aussi etre installe par utilisateur. Le script interroge
donc, dans l'ordre : le `PATH`, les cles de desinstallation du registre
(`InstallLocation`), l'association des fichiers `.iss`, puis les dossiers
`Inno Setup*` des emplacements habituels. En cas d'echec il enumere ce qu'il a
consulte, plutot que de se contenter d'un « introuvable ».

| Chemin | Contenu |
|---|---|
| `dist\AX25Chess\` | build portable, fonctionne tel quel |
| `Output\AX25Chess-<version>-setup.exe` | l'installateur |

Le build est en « un dossier » et non en fichier unique, deliberement : un
onefile se decompresse dans un dossier temporaire a chaque lancement, ce qui
coute plusieurs secondes avec Qt, declenche des heuristiques antivirus et
complique le raisonnement sur le processus fils Direwolf. UPX est desactive
pour la meme raison de robustesse : compresser les DLL Qt est une cause
classique de plantages difficiles a diagnostiquer.

`ax25chess/__init__.py` est la source unique du numero de version.
`make_version.py` en derive `version_info.txt` (la ressource de l'exe) et
`version.iss` (`#define AppVersion`), de sorte que la barre de titre, les
proprietes du fichier et l'installateur ne puissent jamais diverger.

`version_info.txt` est du Python, evalue par PyInstaller. Les valeurs y sont
citees par `repr()` et non par des guillemets poses dans le gabarit : sans
cela, l'apostrophe de « Jeu d'echecs » terminait le litteral et rendait le
fichier insyntaxique. `make_version.py` verifie desormais le resultat par
`ast.parse()` avant de l'ecrire — une erreur de syntaxe ne se serait
manifestee qu'au milieu du build.

### Empaqueter Direwolf dans l'installateur

Deposez le dossier Direwolf telecharge depuis sa page de versions — celui qui
contient `direwolf.exe` — dans un repertoire `direwolf` a la racine du
projet :

```
AX25Chess-1.0.0\
├── direwolf\          <- le dossier telecharge, avec direwolf.exe
├── build_windows.cmd
└── ...
```

`installer.iss` detecte le dossier et ajoute un composant optionnel. Sans ce
dossier, le composant disparait et l'installateur se construit comme avant :
aucune modification a faire dans un cas comme dans l'autre.

Le script se compile donc en **deux variantes**, et les deux doivent tenir
debout toutes seules. Le type d'installation « compact » n'existe par exemple
que si Direwolf est empaquete — il n'aurait aucun sens sinon — et ne doit donc
etre reference qu'a ce moment-la, faute de quoi Inno Setup s'arrete sur
« Parameter "Types" includes an unknown type ». `tests/test_packaging.py`
resout le preprocesseur pour les deux valeurs et verifie chaque variante
separement : un controle global verrait un type declare dans une branche et
employe dans l'autre, et ne prouverait rien.

**Direwolf est installe dans un dossier voisin de celui d'AX25Chess**, et non
a l'interieur :

```
Program Files\
├── AX25Chess\     AX25Chess.exe et _internal\
└── Direwolf\      direwolf.exe
```

C'est un programme a part entiere : il doit pouvoir servir seul ou avec
d'autres logiciels, survivre a la desinstallation d'AX25Chess si vous le
souhaitez, et ne pas etre emporte par la reecriture de `_internal` que
`[InstallDelete]` effectue a chaque montee de version.

L'assistant propose un emplacement — le dossier voisin — et vous laisse le
changer. Il previent avant d'ecraser une installation existante : le dossier
voisin naturel est aussi celui qu'emploie une installation ordinaire de
Direwolf, et l'ecraser sans rien dire remplacerait une version que vous auriez
choisie et configuree.

L'emplacement retenu est note dans `HKA\Software\AX25Chess\DirewolfDir`.
Le desinstallateur le relit — il n'a pas acces aux fonctions `{code:}` de
l'installation — et l'application le consulte en premier, avant d'explorer les
dossiers voisins habituels puis un sous-dossier `direwolf`, ce dernier
couvrant un build portable et les installations anterieures a ce changement.

**Le `direwolf.conf` ne va pas a cote de l'executable.** Une installation
machine atterrit sous `Program Files`, qui n'est pas inscriptible : Direwolf
ne pourrait rien y ecrire et vous ne pourriez pas l'editer sans elever vos
droits. Il est ecrit dans `~/.ax25chess/`, inscriptible quel que soit
l'emplacement d'installation. Le bouton **Creer** de l'onglet RADIO le
regenere a la demande.

**Au premier lancement**, et uniquement si aucun reglage n'a jamais ete
enregistre — donc jamais au-dessus d'un choix deja fait — l'application se
pointe sur le Direwolf embarque, active le lancement et la connexion
automatiques, et ecrit un `direwolf.conf` de depart. Une copie embarquee
l'emporte sur une trouvee dans le `PATH` : c'est celle dont la version a ete
essayee avec ce build.

### Demarrage et liaison KISS : une seule connexion

Direwolf est demarre, puis l'application ouvre directement sa liaison KISS.
C'est **cette liaison** qui sert de signal de disponibilite : elle reessaie
toute seule toutes les 1,5 s, donc les tentatives pendant le demarrage de
Direwolf ne coutent rien. Si le port ne repond toujours pas au bout de 60 s,
l'application le signale et invite a verifier la ligne `KISSPORT`.

Une socket de sondage jetable fonctionnerait aussi pour tester le port, mais
Direwolf la compte comme un client KISS a part entiere : il journalise
« Attached to KISS TCP client application N » puis « has gone away », et elle
occupe un des **trois** emplacements qu'autorise `MAX_NET_CLIENTS` dans
`kiss_frame.h`. L'application n'en ouvre donc pas : quand elle va de toute
facon etablir une vraie liaison, elle passe `probe_port=False` et appelle
`note_ready()` une fois la liaison montee. Le sondage ne subsiste que pour
verifier qu'un TNC ne tourne pas deja avant d'en lancer un second, cas ou il
n'existe aucune liaison a reutiliser.

Pour la meme raison, `_open_link()` est idempotent : un second appel
relancerait `link.open()`, qui abandonne la socket en cours, et Direwolf
verrait un client partir puis un autre arriver.

Journal attendu au demarrage :

```
Ready to accept KISS TCP client application 0 on port 8001 ...
Attached to KISS TCP client application 0 on port 8001 ...
Ready to accept KISS TCP client application 1 on port 8001 ...
```

Un seul `Attached`, aucun `has gone away`.

### La console Direwolf sous Windows

L'onglet DIREWOLF reste le plus souvent vide sous Windows, et c'est normal : le
CRT Microsoft bascule `stdout` en tampon de bloc de 4 Ko dès qu'il n'écrit plus
vers une vraie console mais vers un tuyau. Direwolf n'émet pas 4 Ko à son
démarrage, donc ses lignes n'arrivent jamais. Il n'existe pas d'équivalent
Windows à `stdbuf` pour forcer le vidage depuis l'extérieur. Cela n'empêche pas
la liaison de fonctionner : si le témoin LIAISON est vert, tout va bien.

Pour voir réellement la console, cochez **Fenêtre console Direwolf séparée**
dans l'onglet RADIO. L'option est activée par défaut sous Windows et grisée
sous Linux, où la capture fonctionne.

`QProcess` ne sait pas ouvrir cette fenêtre, et deux tentatives l'ont prouvé.
Qt pose `CREATE_NO_WINDOW` sur les processus qu'il lance ; ajouter
`CREATE_NEW_CONSOLE` via `setCreateProcessArgumentsModifier` ne l'annule pas,
et `startDetached()` n'aide pas davantage, sa structure de démarrage demandant
encore une fenêtre cachée. Dans les deux cas Direwolf tourne, apparaît dans le
gestionnaire des tâches, et n'affiche rien.

Ce chemin n'utilise donc pas `QProcess` du tout. Il utilise `subprocess.Popen`
avec `creationflags=CREATE_NEW_CONSOLE` et **aucune redirection des flux
standards**, la méthode documentée par Microsoft. Ne rien rediriger est aussi
important que le drapeau : rediriger `stdout` ou `stderr` envoie la sortie dans
un tuyau au lieu de la nouvelle console, et la fenêtre s'ouvrirait vide.

Le `-t 0` qui supprime les couleurs ANSI est retiré dans ce mode : dans une
vraie console la couleur est souhaitable, elle n'est du bruit que capturée dans
le journal.

`Popen` conserve un handle, donc `poll()` dit réellement si Direwolf vit encore
— ce qu'un simple PID mémorisé ne permet pas. Fermer sa fenêtre console est
donc remarqué, et arrêter un processus déjà mort ne journalise plus de fausse
erreur. L'arrêt passe par `taskkill /PID <pid> /T /F` : une application console
ne traite pas le `WM_CLOSE` qu'un arrêt courtois posterait, et `/T` emporte la
console avec elle.

Un champ **Commande** affiche en direct la ligne exacte qui sera exécutée,
guillemets compris pour les chemins contenant des espaces :

```
"C:\Program Files\Direwolf\direwolf.exe" -c "C:\Users\f4jtv\direwolf.conf"
```

Si la fenêtre n'apparaît toujours pas, ce champ dit immédiatement si le
problème vient de la commande ou du lancement — et cette ligne se colle telle
quelle dans une invite de commandes pour trancher.

### Installateur bilingue

L'installateur et le desinstallateur sont en anglais et en francais. La langue
suit celle de l'interface Windows (`LanguageDetectionMethod=uilanguage`) et la
boite de choix n'apparait que si la detection echoue
(`ShowLanguageDialog=auto`). Elle est memorisee a l'installation et reutilisee
par le desinstallateur, `UsePreviousLanguage` valant `yes` par defaut.

Les notes d'accueil sont declarees par langue dans `[Languages]` :
`INSTALL-NOTES.en.txt` et `INSTALL-NOTES.fr.txt`. Les libelles propres au
script — types d'installation, composants, questions de desinstallation —
vivent dans `[CustomMessages]`, avec un prefixe `english.` ou `french.`.

### Desinstaller Direwolf

A la desinstallation, une question propose de supprimer Direwolf completement,
dossier compris, ou de le conserver pour un autre programme.

Ce choix n'aurait aucun sens sans un detail : les fichiers du composant
Direwolf portent le drapeau `uninsneveruninstall`. Sans lui, le desinstallateur
retirerait de toute facon ce qu'il a installe, et repondre « non » laisserait
un dossier amputé de son executable — donc inutilisable. Avec ce drapeau, la
suppression est entierement pilotee par `CurUninstallStepChanged`, et repondre
« non » laisse un Direwolf complet et fonctionnel.

Le `DelTree` recursif emporte aussi ce que Direwolf a pu ecrire lui-meme dans
ce dossier, journaux et configuration editee sur place, qu'un desinstallateur
ordinaire ne connaitrait pas.

### Licences

AX25Chess ne contient **aucun** portage de code Direwolf : la couche AX.25 et
l'encapsulation KISS sont ecrites d'apres la specification AX.25 2.2 publique.
AX25Chess n'est donc pas une oeuvre derivee de Direwolf, et le choix de la
GPL-2.0 dans `LICENSE.txt` est celui de l'auteur, pas une obligation heritee.

Les deux programmes s'executent dans des processus separes et communiquent par
une socket TCP : les empaqueter ensemble releve de la simple agregation sur un
support de distribution, au sens du dernier paragraphe de l'article 2 de la
GPL-2.0.

Distribuer le binaire de Direwolf declenche en revanche l'article 3, qui exige
les sources correspondantes ou une offre ecrite de les fournir.
`DIREWOLF-NOTICE.txt` est installe avec le composant et le script de build
refuse de continuer si le binaire est present sans la notice. Lisez-la avant
de diffuser : l'article 3(b) demande une offre venant de **vous** en tant que
distributeur, pas seulement un lien vers le serveur d'un tiers. Deposer
l'archive des sources Direwolf de la meme version a cote de votre installateur
regle la question pour un cout nul.

## 5. Essais sans radio

Le concentrateur fourni simule un canal partagé, avec pertes et latence :

```bash
python3 tools/kiss_hub.py --port 8001 --loss 0.2 --delay 0.5
```

Lancez deux instances d'`AX25Chess` pointant sur ce port, avec deux indicatifs
différents, et jouez une partie complète dans des conditions dégradées.

---

## 6. Validation

```bash
python3 tests/test_chess_rules.py     # perft sur 5 positions de reference
python3 tests/test_protocol.py        # partie complete en boucle AX.25 + KISS
python3 tests/test_direwolf.py        # demarrage automatique de Direwolf
python3 tests/test_games.py           # magasin et reprise des parties
python3 tests/test_packaging.py       # installateur et Direwolf embarque
python3 tests/test_single_client.py   # une seule connexion KISS
python3 tests/test_shutdown.py        # fermeture rapide
python3 tests/test_i18n.py            # bilinguisme
python3 tests/test_endgame.py         # fins de partie recues par radio
python3 tests/test_theme.py           # contrastes des themes
```

- Moteur de règles vérifié par **perft** : position initiale jusqu'à la
  profondeur 4 (197 281 nœuds), Kiwipete, positions 3, 4 et 5.
- Partie complète jouée en boucle locale à travers la pile AX.25 + KISS, avec
  **35 % de pertes** : positions finales identiques des deux côtés.
- Corruption volontaire d'un état suivie d'une `SREQ` : resynchronisation
  réussie et empreintes de nouveau égales.
- Démarrage automatique éprouvé sur un faux Direwolf : lancement, détection du
  port, connexion, non-relance quand un serveur écoute déjà, et comportement en
  cas d'exécutable introuvable.
- Fermeture mesurée sur un faux Direwolf qui **ignore l'arrêt courtois**,
  équivalent Unix d'une application console Windows ignorant le `WM_CLOSE` :
  3006 ms avant correction, 303 ms après, et 0 ms sans Direwolf lancé.
- Mode console séparée éprouvé : handle `Popen` conservé, disponibilité du
  port, arrêt effectif du processus, et détection de la fermeture manuelle de
  la fenêtre par l'opérateur.
- Connexion unique vérifiée sur un faux Direwolf **muet**, qui compte les
  attaches dans un fichier annexe plutôt que sur sa console : c'est la fidélité
  au comportement Windows qui rend ce test probant. Avec le correctif retiré,
  il échoue en reproduisant exactement la séquence observée.

---

## 7. Organisation du code

```
AX25Chess-1.0.0/
├── main.py                        point d'entree
├── lancer.sh                      lanceur avec creation du venv
├── requirements.txt
├── AX25Chess.spec                 recette PyInstaller
├── installer.iss                  script Inno Setup 7
├── build_windows.ps1 / .cmd       chaine de construction Windows
├── make_icon.py                   icone parametrique
├── make_version.py                version a source unique
├── LICENSE.txt                    GPL-2.0 et origine du code
├── INSTALL-NOTES.txt              page d'accueil de l'installateur
├── DIREWOLF-NOTICE.txt            obligations GPL du binaire Direwolf
├── assets/                        ax25chess.ico et .png
├── README.md
├── ax25chess/
│   ├── __init__.py
│   ├── chess_rules.py             moteur d'echecs, suivi des UID, FEN, SAN
│   ├── ax25_kiss.py               trames AX.25 UI, encapsulation KISS
│   ├── protocol.py                protocole CHS-1, machine a etats, resync
│   ├── net_link.py                socket TCP KISS vers Direwolf
│   ├── direwolf.py                lancement et surveillance du processus
│   ├── games.py                   magasin des parties en cours
│   ├── i18n.py                    catalogue et moteur de traduction
│   ├── theme.py                   palettes sombre et claire
│   ├── resources.py               chemins en mode gele, Direwolf embarque
│   ├── game_manager.py            gestionnaire des parties non terminees
│   ├── board_widget.py            echiquier 2D QPainter, badges UID
│   └── main_window.py             fenetre principale, reglages, trames
├── tools/
│   └── kiss_hub.py                concentrateur KISS pour essais sans radio
├── tests/
│   ├── test_chess_rules.py        validation perft
│   ├── test_protocol.py           partie complete en boucle degradee
│   ├── test_direwolf.py           demarrage automatique et repli
│   ├── test_games.py              parties de front, reprise, migration
│   ├── test_single_client.py      une seule connexion KISS chez Direwolf
│   ├── test_shutdown.py           rapidite de fermeture
│   ├── test_i18n.py               couverture et bascule de langue
│   ├── test_endgame.py            nulle, abandon et mat recus
│   ├── test_theme.py              contrastes mesures des deux themes
│   └── test_packaging.py          installateur et installation gelee
└── docs/
    ├── PROTOCOLE.md               specification complete de CHS-1
    └── direwolf.conf.exemple      configuration Direwolf commentee
```

---

## 8. Langues

L'application est bilingue français / anglais. Le sélecteur se trouve dans
l'onglet RADIO ; le changement est immédiat, sans redémarrage, et le choix est
mémorisé. Au premier lancement la langue suit la locale du système, et la
variable d'environnement `AX25CHESS_LANG` (`fr` ou `en`) la force au besoin.

Le circuit habituel de Qt (fichiers `.ts` puis `.qm`) suppose `lrelease`, que
PyQt6 ne fournit pas et qui n'est donc pas garanti sur la machine de
construction. `ax25chess/i18n.py` porte à la place un catalogue Python : il
voyage avec le code, n'ajoute aucune étape au build, se retrouve tel quel dans
le paquet PyInstaller et se relit dans une revue de code. Pour trois cents
chaînes c'est le bon compromis ; au-delà de quelques langues il faudrait
revenir à Qt Linguist.

Les chaînes sources sont en français, comme le code — une entrée absente du
catalogue retombe donc sur du français lisible plutôt que sur une clé
technique. Les champs sont **nommés** et non positionnels, pour qu'une phrase
anglaise puisse réordonner ses éléments :

```python
tr("Coup illegal : {uid} vers la case {case}", uid="WP5", case=29)
#  fr : Coup illegal : WP5 vers la case 29
#  en : Illegal move: WP5 to square 29
```

Le premier parametre de `tr()` est **positionnel uniquement** — d'ou la barre
oblique dans sa signature. Sans elle, un champ portant le meme nom que lui, et
le catalogue emploie bien un champ `text`, leve un `TypeError` au moment
precis ou la chaine doit s'afficher : en pleine partie, pas au demarrage.

`tests/test_i18n.py` appelle **reellement** chaque entree du catalogue avec
ses vrais noms de champs, dans les deux langues, et verifie que chaque valeur
est substituee. Un controle statique des accolades ne l'aurait pas vu. Il
compare aussi le catalogue au code par analyse de l'arbre syntaxique : toute chaîne passée à `tr()` doit avoir une traduction, aucune
entrée ne doit être orpheline, et les champs nommés doivent concorder entre
source et traduction — sinon `tr()` retomberait silencieusement sur le
français à l'exécution.

## 9. Thèmes

Deux thèmes : **sombre** (par défaut) et **clair**, ce dernier conçu pour un
écran en plein soleil. Le sélecteur est dans l'onglet RADIO, le changement est
immédiat et le choix mémorisé.

Le thème clair n'est pas l'inverse du sombre. Sous une lumière forte l'écran
perd du contraste et ce sont les nuances moyennes qui disparaissent d'abord :

- le gris de texte secondaire est nettement plus foncé qu'un simple miroir ne
  le donnerait ;
- l'ambre du cadran VFO, très lisible sur fond sombre, tombe à **2:1** sur du
  blanc ; il est remplacé par un ambre brûlé qui tient à 5,9:1 ;
- le contour des pièces est épaissi et assombri, car c'est lui seul qui détache
  une pièce blanche d'une case ivoire ;
- les identifiants de pièces passent sur une pastille sombre à encre claire :
  une pastille claire se confondrait avec les cases ivoire, et c'est justement
  l'information que ce projet met en avant ;
- le plateau des pièces capturées prend un ton moyen — sur fond clair, une
  pièce blanche s'y perdrait complètement (1,3:1 mesuré).

`ax25chess/theme.py` porte les deux palettes. Les couleurs sont des `QColor`
partagés, mutés sur place au changement de thème : le code de dessin garde ses
références habituelles et n'a rien à savoir du thème courant. La feuille de
style, elle, doit être reconstruite — Qt y fige les couleurs en dur.

`tests/test_theme.py` **mesure** chaque paire texte/fond au rapport de
contraste WCAG 2.1 plutôt que de s'en remettre à l'œil : 4,5:1 pour du texte,
3:1 pour les éléments graphiques porteurs d'information. Il vérifie aussi que
la bascule change réellement le rendu, en comparant des pixels.

## 10. Portabilité

Le code applicatif est portable Windows / Linux. Chaque appel propre à une
plateforme est gardé et possède son équivalent : arrêt de processus
(`taskkill` / `SIGTERM`), fenêtre console séparée (Windows seulement, case
grisée ailleurs), périphérique audio et ligne PTT par défaut du
`direwolf.conf` généré, recherche de l'exécutable Direwolf.

Deux pièges de portabilité méritent d'être signalés, tous deux liés aux
polices :

- **`monospace` est un alias fontconfig.** Il se résout sous Linux, pas sous
  Windows, où Qt retomberait sur une police proportionnelle et ruinerait
  l'alignement de tout l'affichage technique. `mono_family()` retourne une
  famille réellement installée, choisie parmi des candidats puis, à défaut, la
  police à chasse fixe déclarée par le système.
- **`QPainterPath.addText()` ne substitue pas les glyphes manquants**,
  contrairement au dessin de texte ordinaire. Une police dépourvue des
  symboles d'échecs ne donnerait pas des carrés blancs mais un chemin **vide**,
  donc des pièces purement invisibles. `pick_chess_font()` vérifie donc chaque
  glyphe par `QFontMetrics.inFont()` et balaie les familles installées si aucun
  candidat ne convient.

Ce qui n'est **pas** portable, et reste à faire : l'empaquetage. Le projet
fournit une chaîne Windows complète (PyInstaller + Inno Setup) mais rien
d'équivalent sous Linux — pas d'AppImage, pas de `.deb`, pas de `.desktop`.
Sous Linux, l'application se lance depuis les sources avec `./lancer.sh`.

macOS n'est pas visé : le code ne devrait pas en être loin, mais rien n'y a
été essayé.

## 11. Pistes d'extension

- Passage en **AX.25 mode connecté** (I‑frames) si vous préférez déléguer la
  fiabilité à la couche 2 : le protocole CHS‑1 reste valable, il suffit de
  désactiver la couche d'acquittement applicative.
- **Cadence de jeu** transmise dans `HELLO` et pendules affichées.
- Export **PGN** à partir de l'historique, déjà disponible en SAN.
- Diffusion en `CQ` d'une invitation ouverte, avec réponse du premier répondant.
