param([int]$Jobs = 22)
# Pipeline library in shaders.rpf: compute programs into work/shaders/{library,spirv/library} (what make_game's
# library_shaders step does), then the graphics programs (VS, PS, MS) into work/shaders/{library_gfx,spirv/library_gfx}.
$ErrorActionPreference = 'Stop'
$repo = 'C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-int'
$game = 'C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1'
$work = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port\work\shaders'
$log = 'C:\Users\MSI\Desktop\GTA VI\Leonida-port\boot\library_build.log'
$env:LLVM_MC = 'C:\Users\MSI\.leonida\llvm-mc\llvm-mc.exe'
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.363.1'
$env:PATH = "C:\VulkanSDK\1.4.363.1\Bin;$env:PATH"
function Log($m) { "$(Get-Date -Format 'HH:mm:ss') $m" | Tee-Object -FilePath $log -Append }

function Build-Part($stages, $unpackName, $spirvName) {
    $unpacked = Join-Path $work $unpackName
    $spirv = Join-Path $work "spirv\$spirvName"
    Log "unpack $stages -> $unpacked"
    & python -I "$repo\tools\shader_library.py" "$game\shaders.rpf" --out $unpacked --stages $stages --disasm `
        --jobs $Jobs --carve (Join-Path $work 'dxbc') *>> $log
    if ($LASTEXITCODE -ne 0) { Log "unpack $stages FAILED ($LASTEXITCODE)"; return $false }
    New-Item -ItemType Directory -Force $spirv | Out-Null
    Log "translate $stages ($Jobs shards)"
    $procs = @()
    for ($s = 0; $s -lt $Jobs; $s++) {
        $procs += Start-Process -FilePath python -ArgumentList @('-I', "`"$repo\tools\rdna2spirv.py`"",
            "`"$unpacked\disasm`"", '--out', "`"$spirv`"", '--no-asm', '--wave64-emulate', '--shard', "$s/$Jobs") `
            -NoNewWindow -PassThru -RedirectStandardOutput "$spirv\shard$s.out" -RedirectStandardError "$spirv\shard$s.err"
    }
    $procs | Wait-Process
    & python -I "$repo\tools\rdna2spirv.py" "$unpacked\disasm" --out $spirv --merge *>> $log
    Log "translate $stages done (merge exit $LASTEXITCODE)"
    return $true
}

Log '=== library build start'
Build-Part 'cs' 'library' 'library' | Out-Null
Build-Part 'vs,ps,ms' 'library_gfx' 'library_gfx' | Out-Null
Log '=== library build end'
