@echo off
rem Comme lancer.cmd, avec les reglages mesures dans docs\notes-pc-rtx5070ti-2026-11-25.md :
rem - VICTORY_TEXTURE_NO_WAIT=1 : les textures n'attendent plus le verrou de remappage
rem   (une texture peut s'afficher une image plus tard) ;
rem - les reglages de vitesse du README (LATE_WAIT, WIDE_BACK, CARD_BUFFERS).
set "VICTORY_TEXTURE_NO_WAIT=1"
set "VICTORY_LATE_WAIT=3"
set "VICTORY_WIDE_BACK=1"
set "VICTORY_CARD_BUFFERS_MIN_MB=1"
set "VICTORY_CARD_BUFFERS_MB=1024"
call "%~dp0lancer.cmd" %*
