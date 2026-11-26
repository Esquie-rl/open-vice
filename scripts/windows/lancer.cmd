@echo off
setlocal
rem Lance Open Vice sur le jeu, sans reglage particulier.
rem Usage : lancer.cmd [dossier du jeu]   (sinon VICTORY_GAME_DIR, sinon game_dir de victory.toml)
rem Le journal va dans open-vice-derniere-sortie.log, a la racine du depot (ignore par git).
rem Clavier sans manette : Entree = A, Retour arriere = B, fleches = croix, WASD = stick gauche,
rem IJKL = stick droit, Echap = Menu, Tab = View. La fenetre du jeu doit avoir le focus.
cd /d "%~dp0..\.."
if not "%~1"=="" set "VICTORY_GAME_DIR=%~1"
if "%VICTORY_GAME_DIR%"=="" (
  for /f "tokens=2 delims='" %%G in ('findstr /r /c:"^game_dir" victory.toml 2^>nul') do set "VICTORY_GAME_DIR=%%G"
)
if "%VICTORY_GAME_DIR%"=="" (
  echo Indiquez le dossier du jeu : lancer.cmd C:\chemin\du\jeu
  exit /b 1
)
"target\release\open-vice.exe" run "%VICTORY_GAME_DIR%\game_scarlett_master.exe" --execute > "open-vice-derniere-sortie.log" 2>&1
if errorlevel 1 (
  echo Le jeu s'est arrete avec une erreur : voir open-vice-derniere-sortie.log et crashes.log du dossier de donnees.
  pause
)
