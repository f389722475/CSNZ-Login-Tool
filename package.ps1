$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$dist = Join-Path $root 'dist'
$release = Join-Path $dist 'CSNZ_Launcher_1.0.7'
if (-not (Test-Path -LiteralPath (Join-Path $release 'CSNZ登录器.exe'))) { throw 'Run build.ps1 first.' }
$sourceZip = Join-Path $dist 'CSNZ_Launcher_1.0.7_Source.zip'
$deployZip = Join-Path $dist 'CSNZ_Launcher_1.0.7_Deploy.zip'
foreach ($zip in @($sourceZip,$deployZip)) { if (Test-Path -LiteralPath $zip) { throw ('Refusing to overwrite: '+$zip) } }
$stage = Join-Path $root ('evidence\package-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
$sourceStage = Join-Path $stage 'CSNZ_Launcher_1.0.7_Source'
$deployStage = Join-Path $stage 'CSNZ_Launcher_1.0.7'
[IO.Directory]::CreateDirectory($sourceStage) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $deployStage 'licenses')) | Out-Null
# Explicit roots and extension allowlist: no research repos, account settings,
# runtime logs, original game/server binaries, or test databases are published.
foreach ($name in @('README.md','global.json','.gitignore','build.ps1','package.ps1')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $sourceStage $name)
}
foreach ($folder in @('src','tests','assets','native-mod-source','native-launcher-source')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse) {
        $relative = $file.FullName.Substring($root.Length+1)
        if ($relative -match '(^|\\)(bin|obj|build|dist)(\\|$)') { continue }
        if ($file.Extension -notin '.cs','.xaml','.csproj','.manifest','.json','.md','.txt','.h','.c','.cpp','.def','.rc','.cmd','.ps1','.ico','.png','.jpg','.dll' -and $file.Name -ne 'AUTHORS') { continue }
        $to = Join-Path $sourceStage $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to
    }
}
Copy-Item -LiteralPath (Join-Path $release 'CSNZ登录器.exe') -Destination $deployStage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $deployStage '使用说明.md')
foreach ($name in @('MinHook-LICENSE.txt','NOTICE.txt')) {
    Copy-Item -LiteralPath (Join-Path $root ('assets\Native\'+$name)) -Destination (Join-Path $deployStage ('licenses\'+$name))
}
[IO.Compression.ZipFile]::CreateFromDirectory($sourceStage,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$true)
[IO.Compression.ZipFile]::CreateFromDirectory($deployStage,$deployZip,[IO.Compression.CompressionLevel]::Optimal,$true)
Get-Item -LiteralPath $sourceZip,$deployZip | Select-Object FullName,Length
