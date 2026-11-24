param([int]$Rounds = 6, [int]$RunSec = 75)
# Runs the game with LEONIDA_CAPTURE_SHADERS, translates what it captured, and repeats until a run captures nothing new.
$scratch = Split-Path -Parent $MyInvocation.MyCommand.Path
$captured = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port\work\shaders\captured'
$spirv = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port\work\shaders\spirv\captured'
$env:LLVM_MC = 'C:\Users\MSI\.leonida\llvm-mc\llvm-mc.exe'
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.363.1'
$env:PATH = "C:\VulkanSDK\1.4.363.1\Bin;$env:PATH"
for ($r = 1; $r -le $Rounds; $r++) {
    $before = @(Get-ChildItem $captured -Filter *.dxbc -ErrorAction SilentlyContinue).Count
    "=== round $r ($before captured so far)"
    & python -I "$scratch\captured_build.py" $captured $spirv 2>&1 | Select-String -Pattern 'captured containers|translated|FAIL|error' | ForEach-Object { "  $_" }
    & "$scratch\run_diag.ps1" -Tag "loop$r" -TimeoutSec $RunSec -NoTrace -Shots @($RunSec - 5) `
        -ExtraEnv @{ LEONIDA_SKIP_INT3 = '1'; LEONIDA_CAPTURE_SHADERS = $captured } | ForEach-Object { "  $_" }
    $after = @(Get-ChildItem $captured -Filter *.dxbc -ErrorAction SilentlyContinue).Count
    "  captured now: $after"
    if ($after -eq $before) { "no new shader captured: stop"; break }
}
& python -I "$scratch\captured_build.py" $captured $spirv 2>&1 | Select-String -Pattern 'captured containers|translated' | ForEach-Object { "  $_" }
