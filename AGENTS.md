# Guide pour les agents de code

Lisez ceci avant de modifier quoi que ce soit. [CONTRIBUTING.md](CONTRIBUTING.md) donne les règles de tout
contributeur ; ce fichier ajoute ce qu'un agent a tendance à rater.

## Ce qu'est le projet

Open Vice, fork de VIctory : un runtime HLE et une recompilation statique qui exécutent le code x86-64 du jeu
(version Xbox Series 1.0.2.1) sur PC, et traduisent ses shaders RDNA2 (GFX10) en DXIL pour D3D12. Rien du jeu
n'est dans le dépôt ; le joueur fournit sa copie.

## Où sont les choses

- `crates/victory-shader` : le traducteur de shaders (`emit/lower.rs` pour la traduction, `resource.rs` pour
  le suivi des ressources). Une nouvelle règle arrive avec un test synthétique (mots GFX10 écrits à la main).
- `crates/victory-gpu` : la couche D3D12 (copies de la mémoire du jeu, descripteurs, pipelines, draws).
- `crates/victory-hle`, `crates/victory-loader` : modules système et chargeur.
- `apps/open-vice` : l'exécutable (`run`, `translate`, `shader-info`…).

## Règles absolues

- **Aucune donnée du jeu, jamais** : pas de shaders (dans aucun format, désassemblage compris), d'exécutables,
  d'archives, de textures, d'audio, de captures ou de journaux qui les citent, dans les commits, issues,
  pull requests ou fixtures de test. Pour un test, construisez l'entrée à la main.
- **Aucune clé, aucun déchiffrement, aucun contournement de protection.**
- **Rien en ligne** : ni Xbox Live, ni services Rockstar.
- **Corriger plutôt que désactiver** : un shader refusé reçoit une règle, pas une liste d'exclusion. Les rares
  exceptions (comme l'exclusion en bloc du constructeur BVH, qui bloque le GPU) sont documentées et
  désactivables par une variable d'environnement.

## Mesurer avant d'affirmer

- La build `--profile diag --features diagnostics` journalise les raisons de chaque draw abandonné ;
  `VICTORY_CAPTURE_FRAMES` et `VICTORY_DUMP_TARGETS` produisent un rapport d'image et les cibles de rendu,
  `VICTORY_SHADER_DUMP` garde les shaders refusés pour `open-vice translate` hors du jeu. Ces sorties sont des
  données du jeu : elles restent dans le dossier de travail, jamais dans le dépôt ni dans une pull request.
- Une pull request indique ce qui a été mesuré, sur quel GPU, avant et après.

## Avant de pousser

```sh
cargo clippy --workspace --all-targets -- -D warnings
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace
python3 -I scripts/check_no_game_data.py
```

Dites dans la pull request qu'elle a été écrite avec l'aide d'une IA.
