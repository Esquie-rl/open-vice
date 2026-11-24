# Contribuer à Open Vice

Open Vice est un fork de [VIctory](https://github.com/Marvisak/VIctory) qui fait tourner la version
Xbox Series de Grand Theft Auto VI sur PC (Windows, D3D12), à partir de la copie du joueur. Toute
contribution passe par une pull request, relue et acceptée par le propriétaire du dépôt.

## Règles absolues

Une pull request qui enfreint l'une de ces règles est fermée sans relecture.

- **Aucune donnée du jeu, jamais** : ni archives, exécutables, DLL, shaders sous quelque forme que ce soit
  (DXBC, DXIL, désassemblage, SPIR-V), textures, audio, vidéos, captures d'écran ou de cibles de rendu,
  ni journaux ou rapports qui en citent le contenu — dans les commits, les issues, les pull requests et
  les tests. Un test qui a besoin d'un shader construit son entrée à la main (mots d'instructions GFX10,
  comme les tests existants) ou la lit dans le dossier de travail du joueur, hors du dépôt.
- **Aucune clé, aucun déchiffrement, aucun contournement de protection.**
- **Rien en ligne** : ni Xbox Live, ni services ou domaines de Rockstar.
- **Les sorties tirées du jeu restent hors du dépôt** (cache de shaders, journaux, captures) : le fichier
  `.victory-root` et `victory.toml` les placent dans un dossier de données séparé.

Le workflow `No Game Data` vérifie chaque pull request (`scripts/check_no_game_data.py`) ; le test
`repository_passes_policy` de `xtask` aussi.

## Avant de commencer

1. Cherchez dans les issues si le sujet est déjà pris ; sinon ouvrez-en une (ou commentez l'issue de
   coordination épinglée) pour annoncer ce que vous faites.
2. Partez de la branche par défaut, `open-vice-monde`, dans une branche à vous.

## Pull requests

- Une pull request = un sujet. Expliquez le problème, la solution et **comment vous l'avez testé**
  (GPU, pilote, ce qui a été mesuré : shaders traduits avant/après, images par seconde, rapport
  d'image).
- Le CI doit passer : `cargo clippy --workspace --all-targets -- -D warnings`, la même chose avec
  `--all-features`, et `cargo test --workspace`.
- Le style du code suit le dépôt : commentaires en phrases complètes, `shortcut:` pour toute
  approximation assumée, une règle du traducteur arrive avec son test.
- Indiquez si la contribution a été écrite avec l'aide d'une IA.
- La branche par défaut est protégée : pas de push direct ni de force-push, une relue approuvée par le
  propriétaire est exigée, les conversations doivent être résolues.

## Construire et lancer

Voir le [README](README.md) : `cargo build --release -p open-vice`, puis
`target\release\open-vice.exe run <jeu>\game_scarlett_master.exe --execute`.

## Licence

GPL-3.0, comme VIctory : en contribuant, vous acceptez que votre code soit publié sous cette licence.
