# Protocole CHS-1 — spécification

Transport : trames **AX.25 UI** (control `0x03`, PID `0xF0`), non connectées,
émises via l'interface KISS TCP de Direwolf. Charge utile en **ASCII pur**, donc
lisible telle quelle dans un moniteur Direwolf, AGWPE ou un simple `nc`.

## 1. Format de trame

```
CHS1|<gid>|<src>|<dst>|<seq>|<TYPE>|<payload>|<crc>
```

| Champ | Format | Rôle |
|---|---|---|
| `CHS1` | littéral | signature de protocole et version |
| `gid` | 4 hexa | identifiant de partie, tiré au sort par l'initiateur |
| `src` | indicatif | émetteur, avec SSID optionnel |
| `dst` | indicatif | destinataire ; `ALL` ou `CQ` acceptés en réception |
| `seq` | 0..9999 | compteur de trames de l'émetteur |
| `TYPE` | 4 lettres | voir §2 |
| `payload` | champs séparés par `;` | dépend du type |
| `crc` | 4 hexa | CRC16-CCITT (poly `0x1021`, init `0xFFFF`) de tout ce qui précède |

Le CRC applicatif fait doublon avec le FCS AX.25 pour les erreurs binaires, mais
il protège en plus contre les troncatures, les concaténations de trames et les
mauvais décodages KISS.

## 2. Types de trames

| Type | Fiable | Charge utile | Rôle |
|---|:---:|---|---|
| `HELLO` | oui | `<nonce8hex>;<version>` | invitation, tirage des couleurs |
| `ACPT` | oui | `<nonce8hex>;<couleur>` | acceptation, couleur revendiquée |
| `MOVE` | oui | `<UID>;<départ>;<arrivée>;<promo>;<ply>;<empreinte>` | un demi-coup |
| `ACK` | non | `<seq_acquittée>;<empreinte>` | acquittement |
| `SREQ` | non | `<nb_demi_coups_locaux>` | demande de resynchronisation |
| `SYNC` | non | `<n>/<total>;<nb_coups>;<liste>;<gid>` | historique complet, par blocs |
| `RSGN` | oui | — | abandon |
| `DRWO` / `DRWA` / `DRWD` | oui | — | nulle : proposition / acceptation / refus |
| `CHAT` | oui | texte libre | messagerie |
| `PING` / `PONG` | non | jeton | test de liaison |

« Fiable » signifie : acquittement attendu, retransmission jusqu'à obtention.
Une seule trame fiable est en vol à la fois (stop-and-wait).

## 3. Codage d'un coup

```
MOVE|WP5;13;29;-;0;A34F
     │   │  │  │ │  └── CRC16 de la position obtenue APRÈS le coup
     │   │  │  │ └───── numéro de demi-coup, base 0
     │   │  │  └─────── pièce de promotion, ou `-`
     │   │  └────────── case d'arrivée, 1..64
     │   └───────────── case de départ, 1..64 (redondance de contrôle)
     └───────────────── identifiant unique de la pièce
```

La case de départ est redondante : le récepteur connaît déjà la position de
`WP5`. Elle sert de vérification croisée — un désaccord révèle une divergence
d'état avant même d'appliquer le coup.

Le roque est transmis comme un simple coup de roi (`WK1` vers la case 7 ou 3) :
le déplacement de la tour est déduit par le moteur des deux côtés. La prise en
passant et la capture ne demandent aucun champ : elles découlent de la position.

### Numérotation des cases

`numéro = (rang − 1) × 8 + colonne + 1` — a1 = 1, h1 = 8, a8 = 57, h8 = 64.

### Identifiants de pièces

`<couleur><type d'origine><index>` : `WR1 WN1 WB1 WQ1 WK1 WB2 WN2 WR2`,
`WP1..WP8`, et les équivalents `B*` pour les Noirs. L'UID est **immuable** :
une promotion change le type de la pièce, jamais son identifiant.

## 4. Historique compact (SYNC)

Chaque coup tient en `<UID>>​<case>` avec un suffixe `=Q` éventuel :

```
WP5>29,BP5>37,WN2>22,BN1>43,WB2>34,BP1>41,WB2>43,BP4>43
```

Découpage par blocs de 16 coups pour rester sous les 256 octets du champ info.
Le récepteur rejoue la totalité depuis la position initiale, ce qui reconstruit
les UID, les droits de roque, la case de prise en passant et le compteur de
répétitions — informations qu'un instantané FEN perdrait.

## 5. Machine à états

```
   IDLE ──invite()──▶ HANDSHAKE ──ACPT reçu──▶ PLAYING ──mat/nulle/RSGN──▶ OVER
     ▲                    │                       │
     └────HELLO reçu──────┘                       └──SREQ/SYNC──▶ PLAYING
```

Attribution des couleurs : chaque station tire un nonce 32 bits ; le plus grand
prend les Blancs, égalité tranchée par l'ordre alphabétique des indicatifs. Les
deux stations arrivent à la même conclusion sans échange supplémentaire.

## 6. Règles de traitement en réception

1. CRC invalide ou format inconnu → trame ignorée silencieusement.
2. `dst` différent de son propre indicatif (et de `ALL`/`CQ`) → ignorée.
3. `src` différent du correspondant attendu → ignorée avec avertissement.
4. `gid` différent de la partie en cours → ignorée, sauf pour un `HELLO`.
5. Trame fiable déjà vue (même `seq`, même type) → **ré-acquittée sans rejouer**.
6. `MOVE` avec `ply` inférieur à l'attendu → doublon, ré-acquitté.
7. `MOVE` avec `ply` supérieur à l'attendu → trou, déclenche `SREQ`.
8. `MOVE` illégal, case de départ incohérente ou empreinte divergente →
   coup annulé, `SREQ` émis.

## 7. Temporisation

| Paramètre | Défaut | Remarque |
|---|---|---|
| Délai de retransmission | 14 s | réglable de 5 à 120 s dans l'onglet RADIO |
| Gigue aléatoire | 0 à 3 s | évite les collisions de retransmissions |
| Tentatives | 6 | puis alerte opérateur et espacement ×4 |
| Blocs `SYNC` | 16 coups | ~180 octets par trame |

En 1200 bauds VHF, un `MOVE` complet occupe environ 45 octets d'information,
soit une trame unique et un temps d'émission de l'ordre de 0,5 s TXDELAY inclus.
Le délai par défaut laisse largement le temps à l'ACK de revenir.

## 8. Intégration avec Direwolf

L'application peut piloter Direwolf elle-même. Le processus est lancé avec
`-t 0` (console sans codes couleur ANSI) et, si un fichier de configuration est
choisi, `-c <chemin>` ; le répertoire de travail suit ce fichier, ce qui permet
d'utiliser des chemins relatifs dans la configuration.

La disponibilité est détectée en analysant la sortie standard plutôt qu'en
attendant un délai fixe. Les motifs reconnus :

```
accept.*KISS.*(TCP|client)
KISS TCP.*port
Ready to accept
```

et, en cas d'échec évident, ces motifs déclenchent une alerte immédiate :

```
Could not open audio device
Pointless to continue
Config file.*not found
```

Sans annonce reconnue au bout de 6 s, la connexion est tentée malgré tout : une
version de Direwolf au libellé différent ne doit pas bloquer la mise en service.
