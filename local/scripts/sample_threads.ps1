param([int]$Samples = 20, [int]$IntervalMs = 200, [UInt64]$Base = 0x140000000, [UInt64]$Size = 184995840)
# Samples the instruction pointer of every gta6rt thread (suspend, read RIP, resume) and prints, per thread, where in
# the game's image (or outside it) the samples fell.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ThreadRip {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    // The first `count` stack words that fall inside [lo, hi): return addresses into the game, most likely.
    public static ulong[] GameFrames(uint pid, ulong rsp, ulong lo, ulong hi, int count) {
        IntPtr p = OpenProcess(0x0010, false, pid);
        if (p == IntPtr.Zero) return new ulong[0];
        try {
            byte[] buf = new byte[0x2000]; IntPtr got;
            if (!ReadProcessMemory(p, new IntPtr((long)rsp), buf, new IntPtr(buf.Length), out got)) return new ulong[0];
            var list = new System.Collections.Generic.List<ulong>();
            for (int i = 0; i + 8 <= got.ToInt64() && list.Count < count; i += 8) {
                ulong v = BitConverter.ToUInt64(buf, i);
                if (v >= lo && v < hi) list.Add(v);
            }
            return list.ToArray();
        } finally { CloseHandle(p); }
    }
    public static ulong[] Read(uint tid) {
        IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
        if (h == IntPtr.Zero) return null;
        IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
        IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
        try {
            if (SuspendThread(h) == 0xFFFFFFFF) return null;
            Marshal.WriteInt32(ctx, 0x30, 0x100001);  // CONTEXT_CONTROL
            bool ok = GetThreadContext(h, ctx);
            ResumeThread(h);
            if (!ok) return null;
            return new ulong[] { (ulong)Marshal.ReadInt64(ctx, 0xF8), (ulong)Marshal.ReadInt64(ctx, 0x98) };  // Rip, Rsp
        } finally { Marshal.FreeHGlobal(raw); CloseHandle(h); }
    }
}
'@
$p = Get-Process gta6rt -ErrorAction Stop
$hist = @{}
for ($s = 0; $s -lt $Samples; $s++) {
    $p.Refresh()
    foreach ($t in $p.Threads) {
        $r = [ThreadRip]::Read([uint32]$t.Id)
        if (-not $r) { continue }
        $rip = $r[0]
        $where = if ($rip -ge $Base -and $rip -lt $Base + $Size) { 'game+0x{0:X}' -f ($rip - $Base) } else {
            $frames = [ThreadRip]::GameFrames([uint32]$p.Id, $r[1], $Base, $Base + $Size, 4)
            'wait<-' + (($frames | ForEach-Object { 'g+0x{0:X}' -f ($_ - $Base) }) -join ',')
        }
        $key = "$($t.Id)"
        if (-not $hist.ContainsKey($key)) { $hist[$key] = @{} }
        $hist[$key][$where] = 1 + [int]$hist[$key][$where]
    }
    Start-Sleep -Milliseconds $IntervalMs
}
foreach ($tid in $hist.Keys) {
    $top = $hist[$tid].GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3
    $line = ($top | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ' | '
    "tid $tid : $line"
}
