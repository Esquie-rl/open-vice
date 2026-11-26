@echo off
setlocal
rem Traduit d'avance tous les shaders du jeu dans le cache d'Open Vice (1 a 7 minutes, processeur seul).
rem Usage : prechauffer-shaders.cmd [dossier du jeu]
rem VICTORY_HOST_WAVE et VICTORY_VIEW_LIMIT doivent valoir ce que dit le journal d'une partie :
rem "host wave lanes ... max=N" et "host view heap capped at N" (limite = N - 1). Valeurs par defaut :
rem carte NVIDIA (vagues de 32) et tas plafonne a 1 000 000.
cd /d "%~dp0..\.."
if not "%~1"=="" set "VICTORY_GAME_DIR=%~1"
if "%VICTORY_GAME_DIR%"=="" (
  for /f "tokens=2 delims='" %%G in ('findstr /r /c:"^game_dir" victory.toml 2^>nul') do set "VICTORY_GAME_DIR=%%G"
)
if "%VICTORY_HOST_WAVE%"=="" set "VICTORY_HOST_WAVE=32"
if "%VICTORY_VIEW_LIMIT%"=="" set "VICTORY_VIEW_LIMIT=999999"
"target\release\open-vice.exe" translate "%VICTORY_GAME_DIR%\shaders.rpf" --warm all --jobs %NUMBER_OF_PROCESSORS%
pause
