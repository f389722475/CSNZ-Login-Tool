param([switch]$LocalOnly, [switch]$SourceOnly)
$ErrorActionPreference = 'Stop'
if (-not $LocalOnly) { throw 'Public packaging is paused. This candidate embeds installed-game assets; use -LocalOnly only for a private local archive.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$dist = Join-Path $root 'dist'
$release = Join-Path $dist 'CSNZ_Launcher_1.1.1-local1'
if (-not (Test-Path -LiteralPath (Join-Path $release 'CSNZ登录器.exe'))) { throw 'Run build.ps1 first.' }
$sourceZip = Join-Path $dist 'CSNZ_Launcher_1.1.1-local1_Source.zip'
$deployZip = Join-Path $dist 'CSNZ_Launcher_1.1.1-local1_Deploy.zip'
$archives = @($sourceZip)
if (-not $SourceOnly) { $archives += $deployZip }
foreach ($zip in $archives) { if (Test-Path -LiteralPath $zip) { throw ('Refusing to overwrite: '+$zip) } }
$stage = Join-Path $root ('evidence\package-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
$sourceStage = Join-Path $dist 'CSNZ_Launcher_1.1.1-local1_Source'
if (Test-Path -LiteralPath $sourceStage) { throw ('Refusing to overwrite source directory: '+$sourceStage) }
$deployStage = Join-Path $stage 'CSNZ_Launcher_1.1.1-local1'
[IO.Directory]::CreateDirectory($sourceStage) | Out-Null
if (-not $SourceOnly) { [IO.Directory]::CreateDirectory((Join-Path $deployStage 'licenses')) | Out-Null }
# Explicit roots and extension allowlist: no research repos, account settings,
# runtime logs, original game/server binaries, or test databases are published.
foreach ($name in @('README.md','README.en.md','global.json','.gitignore','build.ps1','package.ps1')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $sourceStage $name)
}
foreach ($folder in @('src','tests','assets','weapon-mods','native-launcher-source')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse) {
        $relative = $file.FullName.Substring($root.Length+1)
        if ($relative -match '(^|\\)(bin|obj|build|dist)(\\|$)') { continue }
        if ($file.Extension -notin '.cs','.xaml','.csproj','.manifest','.json','.md','.txt','.h','.hpp','.c','.cpp','.inc','.def','.rc','.cmd','.ps1','.py','.ico','.png','.jpg','.dll','.exe','.nar' -and $file.Name -ne 'AUTHORS') { continue }
        $to = Join-Path $sourceStage $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to
    }
}
# Include the new native implementation, not its databases or runtime evidence.
$awakening = Join-Path $root 'native-awakening-source'
if (-not (Test-Path -LiteralPath $awakening)) { $awakening = Join-Path $root '..\awakening\native' }
foreach ($file in Get-ChildItem -LiteralPath $awakening -Recurse -File) {
    $relative = $file.FullName.Substring(([IO.Path]::GetFullPath($awakening)).Length+1)
    if ($relative -match '(^|\\)(build|bin|obj)(\\|$)') { continue }
    if ($file.Extension -notin '.cpp','.c','.h','.hpp','.cmd','.txt','.md') { continue }
    $to = Join-Path $sourceStage ('native-awakening-source\'+$relative)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $to
}
if (-not $SourceOnly) {
    Copy-Item -LiteralPath (Join-Path $release 'CSNZ登录器.exe') -Destination $deployStage
    foreach ($name in @('README.md','README.en.md')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $deployStage $name)
    }
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $deployStage '使用说明.md')
    foreach ($name in @('MinHook-LICENSE.txt','NOTICE.txt','Auth-NOTICE.txt','Awakening-NOTICE.txt')) {
        Copy-Item -LiteralPath (Join-Path $root ('assets\Native\'+$name)) -Destination (Join-Path $deployStage ('licenses\'+$name))
    }
}
[IO.Compression.ZipFile]::CreateFromDirectory($sourceStage,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$true)
if (-not $SourceOnly) { [IO.Compression.ZipFile]::CreateFromDirectory($deployStage,$deployZip,[IO.Compression.CompressionLevel]::Optimal,$true) }
Write-Host ('Source directory: '+$sourceStage)
Get-Item -LiteralPath $archives | Select-Object FullName,Length
