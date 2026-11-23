param(
    [ValidateSet('dry-run', 'init', 'run')][string]$Mode = 'run',
    [string]$Tag = 'boot',
    [int]$TimeoutSec = 180,
    [string]$Unimpl = 'log',
    [string]$Repo = 'Leonida-boot',
    [switch]$SkipInt3,
    [switch]$Shaders
)
$ErrorActionPreference = 'Stop'
$pkg = 'C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1'
$exe = Join-Path $pkg 'game_scarlett_master.exe'
$rt = Join-Path $pkg "$Repo\runtime\build\gta6rt.exe"
$port = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port'
$bootDir = Join-Path $port 'boot'
$runDir = Join-Path $bootDir "$Tag-$Mode-$(Get-Date -Format 'HHmmss')"
New-Item -ItemType Directory -Force $runDir | Out-Null
$overlay = Join-Path $bootDir 'overlay'
New-Item -ItemType Directory -Force $overlay | Out-Null
$trace = Join-Path $runDir 'startup.jsonl'
$out = Join-Path $runDir 'stdout.log'
$err = Join-Path $runDir 'stderr.log'

$env:LEONIDA_D3D12 = 'vulkan'
$env:LEONIDA_UNIMPLEMENTED = $Unimpl
$env:DISABLE_RTSS_LAYER = '1'
$env:DISABLE_VULKAN_OBS_CAPTURE = '1'
if ($SkipInt3) { $env:LEONIDA_SKIP_INT3 = '1' } else { Remove-Item Env:LEONIDA_SKIP_INT3 -ErrorAction SilentlyContinue }
if ($Shaders) { $env:LEONIDA_SHADERS = Join-Path $port 'work\shaders\spirv' } else { Remove-Item Env:LEONIDA_SHADERS -ErrorAction SilentlyContinue }

$argList = @("--$Mode", "`"$exe`"", '--game-dir', "`"$pkg`"", '--overlay', "`"$overlay`"", '--trace', "`"$trace`"")
if ($Mode -eq 'run') { $argList += '--trace-d3d12' }
$p = Start-Process -FilePath $rt -ArgumentList $argList -WorkingDirectory $runDir -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $out -RedirectStandardError $err
$null = $p.Handle
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit()
    "TIMEOUT after $TimeoutSec s (killed)"
}
"exit=0x{0:X8} ({1})" -f $p.ExitCode, $p.ExitCode
"dir=$runDir"
