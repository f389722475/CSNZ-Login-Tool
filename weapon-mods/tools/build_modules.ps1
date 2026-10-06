$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$catalog = Get-Content -LiteralPath (Join-Path $root 'catalog.json') -Raw | ConvertFrom-Json
$coreDir = Join-Path $root 'build/core'
if (!(Test-Path -LiteralPath (Join-Path $coreDir 'CSNZWeaponCore.lib'))) { throw 'Run build-core.cmd first' }
Push-Location $coreDir
try {
    & link /NOLOGO /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /OPT:ICF /DEF:"$root/shared/core.def" /OUT:"$root/shared/CSNZWeaponCore.dll" /IMPLIB:CSNZWeaponCore.exports.lib runtime.obj CSNZWeaponCore.lib bcrypt.lib
    if ($LASTEXITCODE) { throw 'Core DLL link failed' }
    $outputs = @('shared/CSNZWeaponCore.dll')
    foreach ($weapon in $catalog.weapons) {
        $dir = Join-Path $root $weapon.folder
        [void][System.IO.Directory]::CreateDirectory($dir)
        $source = "// Named module for $($weapon.name) ($($weapon.id)); implementation is shared by family.`r`n#define CSNZ_WEAPON_ID $($weapon.id)`r`n#include `"../shared/plugin.cpp`"`r`n"
        [System.IO.File]::WriteAllText((Join-Path $dir 'module.cpp'), $source, [System.Text.UTF8Encoding]::new($false))
        $weapon | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'weapon.json') -Encoding UTF8
        & cl /nologo /LD /std:c++17 /utf-8 /O2 /MT /W4 /EHsc /Fo:"$($weapon.folder).obj" "$dir/module.cpp" /link /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /DEF:"$root/shared/plugin.def" /OUT:"$dir/$($weapon.folder).dll" /IMPLIB:"$($weapon.folder).lib"
        if ($LASTEXITCODE) { throw "Module DLL build failed: $($weapon.folder)" }
        $outputs += "$($weapon.folder)/$($weapon.folder).dll"
    }
    $entries = foreach ($relative in $outputs) {
        $file = Get-Item -LiteralPath (Join-Path $root $relative)
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $input = [System.IO.File]::OpenRead($file.FullName)
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($input)).Replace('-','').ToLowerInvariant() }
        finally { $input.Dispose(); $sha.Dispose() }
        [ordered]@{ path=$relative; bytes=$file.Length; sha256=$digest }
    }
    [ordered]@{schema=1; version=$catalog.version; architecture='x86'; implementation='native-cpp'; files=@($entries)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'bundle-manifest.json') -Encoding UTF8
} finally { Pop-Location }
Write-Host ("Built {0} named native weapon DLLs and one shared server core." -f $catalog.weapons.Count)
