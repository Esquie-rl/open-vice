# Open Vice

Fait tourner **Grand Theft Auto VI (Xbox Series X|S, 1.0.2.1)** nativement sous Windows, à partir des fichiers de
**ta propre copie** du jeu. Aucun fichier du jeu n'est dans ce dépôt.

Open Vice est un fork de **[VIctory](https://github.com/Marvisak/VIctory)** (Marvisak et ses contributeurs,
GPL-3.0-or-later), adapté pour tourner sur un PC **Intel + NVIDIA** (VIctory n'était testé que sur une AMD RX 6700 XT).
Le code x86-64 du jeu tourne tel quel dans notre processus ; Open Vice fournit la plateforme Xbox en dessous et
convertit les données du jeu (shaders console traduits en DXIL au premier usage, puis mis en cache). Architecture :
[docs/architecture.md](docs/architecture.md).

## État (24/11/2026, i9-12900HX + RTX 4080 Laptop, Windows 11)

**Pas encore jouable.**

| Étape | État sur ce PC |
|-------|----------------|
| Démarrage, écrans de premier lancement (profil, navigation, réglages…) | marche, 30 images/s, interface en français |
| Son | marche : sons d'interface et musique enregistrés (`VICTORY_AUDIO_CAPTURE`) |
| Écran titre, profil, chargement | marche |
| Entrée dans le monde | le GPU décroche (`device removed`) pendant les premières soumissions du monde, comme sur la machine de VIctory |

## Ce qu'Open Vice change par rapport à VIctory

- **CPU Intel** : le jeu efface la mémoire avec des boucles `clzero` (instruction AMD). Sur Intel, chaque ligne de
  64 octets coûtait une exception (~5 s par image). Au premier passage, chaque boucle est réécrite dans le code chargé
  en un appel vers une routine de 8 écritures par ligne (`crates/victory-loader/src/crash.rs`).
- **Nom** : fenêtre, exécutable (`open-vice.exe`), messages de crash et nom du GPU affiché.

Les noms internes de VIctory sont **conservés** pour ne rien casser : crates `victory-*`, variables `VICTORY_*`,
fichier `victory.toml`, dossiers `%LOCALAPPDATA%\victory`, marqueur `.victory-root`.

## Prérequis

Windows 10/11 x64, un CPU avec AVX2, un GPU Direct3D 12 shader model 6.6 (et mesh shaders pour le monde), Rust
(version épinglée dans `rust-toolchain.toml`, installée toute seule par rustup), plusieurs Gio de mémoire virtuelle
disponible (si `crashes.log` signale l'erreur Windows 1455, agrandir le fichier d'échange).

## Compiler et lancer

```powershell
copy victory.example.toml victory.toml   # puis mettre tes chemins (hors du dépôt)
cargo test --workspace                   # aucun fichier du jeu nécessaire
cargo run -p open-vice -- config         # vérifie les chemins

cargo build --release -p open-vice
target\release\open-vice.exe run <dossier du jeu>\game_scarlett_master.exe --execute
```

Build de diagnostic (captures d'images, touches automatiques, couche de debug D3D12) :

```powershell
cargo build --profile diag -p open-vice --features diagnostics   # target\diag\open-vice.exe
```

La barre de titre affiche les images/s. Sauvegardes, réglages et `crashes.log` : dossier de données (par défaut
`%LOCALAPPDATA%\victory\data`).

## Contrôles

Manettes XInput. Sans manette, le clavier sert de manette quand la fenêtre du jeu a le focus :

| Touches | Manette |
|---------|---------|
| W A S D / I J K L | stick gauche / droit |
| flèches | croix |
| Entrée, Retour arrière, X, Y | A, B, X, Y |
| Q / E, Z / C | gâchettes hautes, gâchettes |
| F / H | clics des sticks |
| Échap, Tab | Menu, Vue |

## Variables de diagnostic (build `diag`)

| Variable | Effet |
|----------|-------|
| `VICTORY_KEYS=15:Enter,20:Enter` | appuie sur des touches à ces secondes, focus ou pas |
| `VICTORY_AUDIO_CAPTURE=<fichier.wav>` | enregistre le son joué par le jeu |
| `VICTORY_CAPTURE=<dossier>` | enregistre des images présentées |
| `VICTORY_D3D12_DEBUG=1` | couche de debug D3D12 (nomme la commande qui fait décrocher le GPU) |
| `VICTORY_D3D12_WARP=1` | rendu logiciel (lent, ne peut pas planter le pilote) |
| `VICTORY_LOG=<cible>=debug,...` | journaux détaillés |

La liste complète est dans le README d'origine de VIctory.

## Contribuer

1. Clonez le dépôt et ouvrez-le avec votre agent de code (Claude Code lit `CLAUDE.md`, les autres `AGENTS.md`) : les règles et la méthode de travail y sont.
2. Annoncez votre sujet dans l'issue épinglée **« Coordination : qui travaille sur quoi »**.
3. Créez une branche, puis ouvrez une pull request vers `open-vice-monde` : le contrôle « No Game Data » et la relecture du propriétaire sont obligatoires.

Règle n° 1 : **aucune donnée du jeu** dans le dépôt, les issues ou les pull requests. Chacun utilise sa propre copie du jeu, en local. Le détail est dans [CONTRIBUTING.md](CONTRIBUTING.md).
## Licence et crédits

GPL-3.0-or-later, voir [LICENSE](LICENSE). Basé sur VIctory (https://github.com/Marvisak/VIctory) ; les
modifications d'Open Vice sont dans l'historique git de ce dépôt.
