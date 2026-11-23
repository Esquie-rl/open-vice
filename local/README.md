# Open Vice — branche de travail pour faire démarrer GTA VI avec Leonida

Branche privée basée sur [ItsNotRexed/Leonida](https://github.com/ItsNotRexed/Leonida) (`main` du 23/11/2026),
avec des PR pas encore fusionnées et quelques corrections locales, pour faire avancer le démarrage de
`game_scarlett_master.exe` (Xbox Series 1.0.2.1) sous `gta6rt`.

## Règle absolue

**Aucune donnée du jeu ici** : ni archives, ni exécutable, ni shaders (DXBC, FSSM, désassemblage, SPIR-V), ni
textures, ni traces `startup.jsonl` / `stderr.log` (elles citent des adresses et des chaînes du jeu). Chacun
travaille avec **sa propre copie** du jeu et garde ses fichiers de travail chez lui. On partage du code, des
comptes et des formes d'appels, rien d'autre.

## Où on en est

Avec le runtime de cette branche (`LEONIDA_D3D12=vulkan`, RTX 4080 Laptop), le titre passe :

- mémoire console (`xmem`, pages physiques, placeholders), TLS index 0, DirectStorage, services GDK hors ligne ;
- device D3D12 sur Vulkan : 5 queues, 581 command lists, descriptor heap à l'adresse du titre, vues, cible 4K ;
- blocage 17 (contrôle de budget) : sauté avec `LEONIDA_SKIP_INT3=1` ;
- **blocage 18** : `XboxBvhBuilder.dll` crée ses pipelines de calcul → il faut les shaders traduits
  (`LEONIDA_SHADERS=<work>/shaders/spirv`). La traduction est en cours sur la machine d'origine.

Ailleurs (fil de coordination du projet), d'autres sont allés jusqu'à la boucle d'images (`PresentX` en boucle) sur
AMD RDNA2.

## Contenu de la branche

Rebasée (par merge) sur `main` amont `dfe4e4a` du 23/11/2026. Déjà dans `main` amont depuis : #6919, #6908,
#6935, #6891 (`xg_xs.cpp`), #1864, #4473 et les correctifs du traducteur #6685, #6635, #6714, #5206.

PR amont encore appliquées localement (diff propre de la PR) :

- ouvertes en amont : #6582, #7009, #2490, #6953, #7075, #7021, #7066, #7052, #7110, #7078, #7115, #7290, #7163,
  #7323, #7403, #7685, #7642, #7556, #7529 ;
- fermées sans fusion en amont, gardées ici : #7017, #4313, #4348, #6973, #5680.

#6279 a été retirée (doublon de #1864).

`xmem.cpp` est maintenant la version amont (#6908 + #6919 telles que fusionnées) avec #7556 par-dessus. Différences
avec l'ancienne version locale : `PAGE_WRITECOMBINE` n'est plus retiré de la protection (seulement les bits
`PAGE_GRAPHICS_*`), et l'événement de trace `xmem_virtual_alloc` n'existe plus (`XMemVirtualAlloc` passe par
`xmem_attributes`). À surveiller au prochain lancement : lectures CPU lentes sur de la mémoire write-combined.

Corrections locales :

- slot 48 `CreateCommandQueueX` : ne garde que le préfixe PC de la description (flag PC, priorité PC, nœud 0) ;
- slot 73 `CreateCommandListX` : l'IID console reçoit le proxy de liste console de #2490 ;
- `PresentX` : la fenêtre « Leonida » est **affichée** (1600×900, fenêtrée) et ses messages sont traités à chaque
  `PresentX` (avant, elle était créée invisible) ;
- `tests/semantic/test_image_atomics_guard.py` : accepte la valeur « active » mise en cache (#4473) comme garde.

## Construire et lancer

Prérequis : VS 2022 (C++), Python 3.12, Vulkan SDK, `llvm-mc` avec AMDGPU (`scripts/build_llvm_mc.ps1`), ta copie du
jeu.

```powershell
# runtime (télécharge les en-têtes Vulkan et SDL3 épinglés)
cmake -S runtime -B runtime/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DLEONIDA_VULKAN_HEADERS_FETCH=ON -DLEONIDA_REQUIRE_VULKAN=ON -DLEONIDA_SDL3_FETCH=ON
cmake --build runtime/build --target gta6rt gta6rt_tls_host

# shaders (long : ~1 h 30 sur 24 threads), --out et --work HORS du dossier du jeu
python -I make_game.py --game <ta copie> --out <port> --work <port>\work --stages check,shaders --jobs <threads>
```

`local/scripts/` contient les scripts utilisés ici (chemins à adapter) : `run_boot.ps1` (un lancement avec traces),
`run_boot_video.ps1` (lancement + vidéo du bureau avec ffmpeg), `build_boot.cmd`, `run_shaders.cmd`.

## Tâches lourdes à prendre

1. **Suite de tests du traducteur complète** (`python -m unittest discover -s tests/semantic`, ~70 min) et liste des
   échecs. Connus : 3 tests NaN min3/max3/med3 corrigés par #6685 ; à confirmer qu'il n'en reste aucun.
2. **Traduction des shaders sur ta copie**, puis lancements avec `LEONIDA_SHADERS` : noter le dernier appel et
   l'exception (`tools/boot_trace.py startup.jsonl --d3d12 stderr.log`). Une carte AMD RDNA2/RDNA3 est la plus utile
   (wave64 natif).
3. **Blocages suivants** : 19-25 sont couverts par les PR ci-dessus ; 26 (device slot 57, pipelines natifs) et la
   boucle d'images sont ouverts. Intégrer les nouvelles PR du boot au fur et à mesure.
4. **Rebase sur le `main` amont** quand il a intégré une partie de ces PR.
