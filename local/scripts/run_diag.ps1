param(
    [string]$Tag = 'diag',
    [int]$TimeoutSec = 150,
    [string]$Repo = 'Leonida-int',
    [switch]$NoTrace,
    [switch]$NoStubPipelines,
    [hashtable]$ExtraEnv = @{},
    [int[]]$Shots = @()
)
# Same settings as run_boot_video.ps1, without the screen recording.
$ErrorActionPreference = 'Stop'
$pkg = 'C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1'
$exe = Join-Path $pkg 'game_scarlett_master.exe'
$rt = Join-Path $pkg "$Repo\runtime\build\gta6rt.exe"
$port = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port'
$bootDir = Join-Path $port 'boot'
$runDir = Join-Path $bootDir "$Tag-run-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
New-Item -ItemType Directory -Force $runDir | Out-Null
$overlay = Join-Path $bootDir 'overlay'
New-Item -ItemType Directory -Force $overlay | Out-Null

$env:LEONIDA_D3D12 = 'vulkan'
$env:LEONIDA_UNIMPLEMENTED = 'log'
$env:DISABLE_RTSS_LAYER = '1'
$env:DISABLE_VULKAN_OBS_CAPTURE = '1'
Remove-Item Env:LEONIDA_SKIP_INT3 -ErrorAction SilentlyContinue
$env:LEONIDA_SHADERS = Join-Path $port 'work\shaders\spirv'
if ($NoStubPipelines) { Remove-Item Env:LEONIDA_STUB_PIPELINES -ErrorAction SilentlyContinue } else { $env:LEONIDA_STUB_PIPELINES = '1' }
foreach ($k in $ExtraEnv.Keys) { Set-Item "Env:$k" $ExtraEnv[$k] }

$trace = Join-Path $runDir 'startup.jsonl'
$argList = @('--run', "`"$exe`"", '--game-dir', "`"$pkg`"", '--overlay', "`"$overlay`"", '--trace', "`"$trace`"")
if (-not $NoTrace) { $argList += '--trace-d3d12' }
$p = Start-Process -FilePath $rt -ArgumentList $argList -WorkingDirectory $runDir -PassThru -WindowStyle Normal `
    -RedirectStandardOutput (Join-Path $runDir 'stdout.log') -RedirectStandardError (Join-Path $runDir 'stderr.log')
$null = $p.Handle
$started = Get-Date
if ($Shots.Count) {
    Add-Type -AssemblyName System.Drawing
    if (-not ('LeonidaShot' -as [type])) {
        Add-Type @'
using System; using System.Runtime.InteropServices; using System.Drawing;
public static class LeonidaShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
    public static bool SaveScreen(IntPtr h, string path) {
        ShowWindow(h, 9); SetForegroundWindow(h); System.Threading.Thread.Sleep(400);
        RECT r; if (!GetWindowRect(h, out r) || r.R <= r.L || r.B <= r.T) return false;
        using (var bmp = new Bitmap(r.R - r.L, r.B - r.T)) using (var g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(r.L, r.T, 0, 0, bmp.Size);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
        return true;
    }
    public static bool Save(IntPtr h, string path) {
        RECT r; if (!GetClientRect(h, out r) || r.R <= 0 || r.B <= 0) return false;
        using (var bmp = new Bitmap(r.R, r.B)) using (var g = Graphics.FromImage(bmp)) {
            IntPtr dc = g.GetHdc(); bool ok = PrintWindow(h, dc, 3); g.ReleaseHdc(dc);
            if (ok) bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            return ok;
        }
    }
}
'@ -ReferencedAssemblies System.Drawing
    }
    foreach ($t in ($Shots | Sort-Object)) {
        $wait = $t - ((Get-Date) - $started).TotalSeconds
        if ($wait -gt 0 -and $p.WaitForExit([int]($wait * 1000))) { break }
        $h = [LeonidaShot]::FindWindowW([NullString]::Value, 'Leonida')
        if ($h -eq [IntPtr]::Zero) { "shot $t s: no window"; continue }
        $png = Join-Path $runDir ("shot_{0:D3}.png" -f $t)
        "shot $t s: $([LeonidaShot]::Save($h, $png))"
        $png2 = Join-Path $runDir ("screen_{0:D3}.png" -f $t)
        "screen $t s: $([LeonidaShot]::SaveScreen($h, $png2))"
    }
}
$left = $TimeoutSec - ((Get-Date) - $started).TotalSeconds
if ($left -lt 1) { $left = 1 }
if (-not $p.WaitForExit([int]($left * 1000))) {
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit()
    "TIMEOUT after $TimeoutSec s (killed)"
}
"game exit=0x{0:X8} after {1:N0} s" -f $p.ExitCode, ((Get-Date) - $started).TotalSeconds
"dir=$runDir"
