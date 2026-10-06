param(
    [string]$OutputDirectory,
    [string]$ZipPath
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'src' }
$target = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $target) { throw ('Refusing to overwrite an existing source directory: ' + $target) }
if ($ZipPath) {
    $ZipPath = [IO.Path]::GetFullPath($ZipPath)
    if (Test-Path -LiteralPath $ZipPath) { throw ('Refusing to overwrite an existing archive: ' + $ZipPath) }
}
$catalog = Get-Content -LiteralPath (Join-Path $root 'catalog.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$extensions = @('.c','.cpp','.h','.hpp','.inc','.def','.cmd','.ps1','.py','.json','.md','.txt')
# Gather only authoring roots before creating the destination. No DLLs, installed
# game assets, build outputs, account databases, runtime logs or private evidence.
$files = @(foreach ($name in @('README.md','README_SOURCE.md','catalog.json','asset-manifest.json','build-core.cmd','build-native.cmd','build-tests.cmd')) {
    Get-Item -LiteralPath (Join-Path $root $name)
})
foreach ($folder in @('shared','profiles','tests','tools') + @($catalog.weapons | ForEach-Object { $_.folder })) {
    $files += @(Get-ChildItem -LiteralPath (Join-Path $root $folder) -Recurse -File | Where-Object {
        $_.FullName -notmatch '(?i)[\\/](build|bin|obj|logs|__pycache__)[\\/]' -and
        ($_.Extension -in $extensions -or $_.Name -eq 'AUTHORS')
    })
}
[IO.Directory]::CreateDirectory($target) | Out-Null
foreach ($file in $files) {
    $relative = $file.FullName.Substring($root.Length + 1)
    $to = Join-Path $target $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $to
}
if ($ZipPath) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ZipPath)) | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($target,$ZipPath,[IO.Compression.CompressionLevel]::Optimal,$true)
}
Write-Host ('Source ready: {0} ({1} files, {2} weapon modules)' -f $target,$files.Count,$catalog.weapons.Count)
if ($ZipPath) { Get-Item -LiteralPath $ZipPath | Select-Object FullName,Length }
