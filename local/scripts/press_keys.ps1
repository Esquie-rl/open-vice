param([int]$StartSec = 40, [int]$EverySec = 5, [int]$Count = 14)
# Presses Enter then Space in the game's "Leonida" window every few seconds, only when that window is the foreground
# window (checked before each key, so no other program receives them). Prints the game's disk reads as it goes.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class KeyPoke {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public static bool Press(IntPtr h, byte vk) {
        ShowWindow(h, 9); SetForegroundWindow(h); System.Threading.Thread.Sleep(150);
        if (GetForegroundWindow() != h) return false;
        keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(120);
        keybd_event(vk, 0, 2, UIntPtr.Zero);
        return true;
    }
}
'@
Start-Sleep -Seconds $StartSec
for ($i = 0; $i -lt $Count; $i++) {
    $h = [KeyPoke]::FindWindowW([NullString]::Value, 'Leonida')
    $w = Get-CimInstance Win32_Process -Filter "Name='gta6rt.exe'" | Select-Object -First 1
    if (-not $w) { 'game gone'; break }
    $enter = if ($h -ne [IntPtr]::Zero) { [KeyPoke]::Press($h, 0x0D) } else { $false }
    $space = if ($h -ne [IntPtr]::Zero) { [KeyPoke]::Press($h, 0x20) } else { $false }
    "t+{0}s enter={1} space={2} read={3:N0} MB" -f ($StartSec + $i * $EverySec), $enter, $space, ($w.ReadTransferCount / 1MB)
    Start-Sleep -Seconds $EverySec
}
