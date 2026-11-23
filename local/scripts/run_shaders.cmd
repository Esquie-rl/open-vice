@echo off
chcp 65001 >nul
rem Attend la fin du deplacement de Leonida-port hors du dossier du jeu, puis traduit les shaders.
:wait
if exist "C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-port\" (
  timeout /t 5 /nobreak >nul
  goto wait
)
set "LLVM_MC=C:\Users\MSI\.leonida\llvm-mc\llvm-mc.exe"
set "VULKAN_SDK=C:\VulkanSDK\1.4.363.1"
set "PATH=C:\VulkanSDK\1.4.363.1\Bin;%PATH%"
set "LOG=C:\Users\MSI\Desktop\GTA VI\Leonida-port\boot\shaders_stage.log"
cd /d "C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-shaders" || exit /b 1
echo debut %date% %time% > "%LOG%"
python -I make_game.py --game "C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1" --out "C:\Users\MSI\Desktop\GTA VI\Leonida-port" --work "C:\Users\MSI\Desktop\GTA VI\Leonida-port\work" --stages check,shaders --jobs 24 --no-tui >> "%LOG%" 2>&1
echo SHADERS_EXIT=%errorlevel% %date% %time% >> "%LOG%"
