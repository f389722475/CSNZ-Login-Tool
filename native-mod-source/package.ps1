param([string]$DistRoot = (Join-Path $PSScriptRoot 'dist'), [string]$ReleaseSuffix = '-pathfix1')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -Force
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$dist = [IO.Path]::GetFullPath($DistRoot)
$prefix = 'CSNZ_GigaBreakLE_Native_0.7.4-r2' + $ReleaseSuffix
$sourceZip = Join-Path $dist ($prefix + '_Source.zip')
$deployZip = Join-Path $dist ($prefix + '_Deploy.zip')
foreach ($file in @($sourceZip,$deployZip)) { if (Test-Path -LiteralPath $file) { throw ('Refusing to overwrite: '+$file) } }
foreach ($name in @('GigaBreakLE.dll','CSNZ_GigaBreakLE.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root ('build\bin\'+$name)) -PathType Leaf)) { throw 'Run build.cmd first.' }
}
[IO.Directory]::CreateDirectory($dist) | Out-Null
$stage = Join-Path $root ('build\package-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fffffff'))
$source = Join-Path $stage ($prefix+'_Source')
$deploy = Join-Path $stage ($prefix+'_Deploy')
[IO.Directory]::CreateDirectory($source) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $deploy 'payload')) | Out-Null
foreach ($name in @('build.cmd','package.ps1','README_NATIVE.md','NOTICE.txt')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $source $name)
}
# Explicit source roots only. Never crawl the parent project or copy a configured
# installation, baselines, evidence, logs, build products or account files.
foreach ($folder in @('src','third_party\minhook','package')) {
    $from = Join-Path $root $folder
    foreach ($file in Get-ChildItem -LiteralPath $from -Recurse -File) {
        $relative = $file.FullName.Substring($root.Length+1)
        $to = Join-Path $source $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to
    }
}
foreach ($name in @('Install.cmd','Install.ps1','Start_Mod.cmd','Stop_Mod.cmd','README_zh-CN.txt')) {
    Copy-Item -LiteralPath (Join-Path $root ('package\'+$name)) -Destination (Join-Path $deploy $name)
}
foreach ($name in @('GigaBreakLE.dll','CSNZ_GigaBreakLE.exe')) {
    Copy-Item -LiteralPath (Join-Path $root ('build\bin\'+$name)) -Destination (Join-Path $deploy ('payload\'+$name))
}
Copy-Item -LiteralPath (Join-Path $root 'NOTICE.txt') -Destination (Join-Path $deploy 'NOTICE.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\minhook\LICENSE.txt') -Destination (Join-Path $deploy 'MinHook-LICENSE.txt')
[IO.Compression.ZipFile]::CreateFromDirectory($source,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$true)
[IO.Compression.ZipFile]::CreateFromDirectory($deploy,$deployZip,[IO.Compression.CompressionLevel]::Optimal,$true)
Get-Item -LiteralPath $sourceZip,$deployZip | Select-Object FullName,Length
