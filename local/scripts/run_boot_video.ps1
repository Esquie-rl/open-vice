param(
    [string]$Tag = 'video',
    [int]$TimeoutSec = 240,
    [string]$Repo = 'Leonida-boot',
    [switch]$SkipInt3,
    [switch]$NoShaders
)
# Records the whole Windows desktop (ffmpeg, Desktop Duplication) while gta6rt runs the game in its window.
$ErrorActionPreference = 'Stop'
$pkg = 'C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1'
$exe = Join-Path $pkg 'game_scarlett_master.exe'
$rt = Join-Path $pkg "$Repo\runtime\build\gta6rt.exe"
$port = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port'
$bootDir = Join-Path $port 'boot'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $bootDir "$Tag-run-$stamp"
$videoDir = Join-Path $bootDir 'videos'
New-Item -ItemType Directory -Force $runDir, $videoDir | Out-Null
$overlay = Join-Path $bootDir 'overlay'
New-Item -ItemType Directory -Force $overlay | Out-Null
$video = Join-Path $videoDir "leonida-$stamp.mp4"

# 1. Screen recording.
$ff = New-Object System.Diagnostics.ProcessStartInfo
$ff.FileName = (Get-Command ffmpeg).Source
$ff.Arguments = "-hide_banner -loglevel error -y -f lavfi -i `"ddagrab=output_idx=0:framerate=30:draw_mouse=1,hwdownload,format=bgra`" -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -movflags +faststart `"$video`""
$ff.UseShellExecute = $false
$ff.RedirectStandardInput = $true
$ff.RedirectStandardError = $true
$ff.CreateNoWindow = $true
$rec = [System.Diagnostics.Process]::Start($ff)
$ffErr = $rec.StandardError.ReadToEndAsync()
Start-Sleep -Seconds 2

# 2. The game.
$env:LEONIDA_D3D12 = 'vulkan'
$env:LEONIDA_UNIMPLEMENTED = 'log'
$env:DISABLE_RTSS_LAYER = '1'
$env:DISABLE_VULKAN_OBS_CAPTURE = '1'
if ($SkipInt3) { $env:LEONIDA_SKIP_INT3 = '1' } else { Remove-Item Env:LEONIDA_SKIP_INT3 -ErrorAction SilentlyContinue }
if ($NoShaders) { Remove-Item Env:LEONIDA_SHADERS -ErrorAction SilentlyContinue } else { $env:LEONIDA_SHADERS = Join-Path $port 'work\shaders\spirv' }
$trace = Join-Path $runDir 'startup.jsonl'
$argList = @('--run', "`"$exe`"", '--game-dir', "`"$pkg`"", '--overlay', "`"$overlay`"", '--trace', "`"$trace`"", '--trace-d3d12')
$p = Start-Process -FilePath $rt -ArgumentList $argList -WorkingDirectory $runDir -PassThru -WindowStyle Minimized `
    -RedirectStandardOutput (Join-Path $runDir 'stdout.log') -RedirectStandardError (Join-Path $runDir 'stderr.log')
$null = $p.Handle
$started = Get-Date
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit()
    "TIMEOUT after $TimeoutSec s (killed)"
}
"game exit=0x{0:X8} after {1:N0} s" -f $p.ExitCode, ((Get-Date) - $started).TotalSeconds

# 3. Stop the recording cleanly.
Start-Sleep -Seconds 3
$rec.StandardInput.Write('q')
$rec.StandardInput.Close()
if (-not $rec.WaitForExit(30000)) { $rec.Kill() }
$e = $ffErr.Result
if ($e) { "ffmpeg: $e" }
"video=$video"
"dir=$runDir"
