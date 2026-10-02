param([switch]$RebuildNative)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
if ($RebuildNative -or -not (Test-Path -LiteralPath (Join-Path $root 'assets\Native\CSNZLauncherBridge.dll'))) {
    & (Join-Path $root 'native-launcher-source\build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'Authentication bridge build failed.' }
    Copy-Item -LiteralPath (Join-Path $root 'native-launcher-source\build\bin\CSNZLauncherBridge.dll') -Destination (Join-Path $root 'assets\Native\CSNZLauncherBridge.dll')
}
if ($RebuildNative) {
    & (Join-Path $root 'native-mod-source\build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    Copy-Item -LiteralPath (Join-Path $root 'native-mod-source\build\bin\GigaBreakLE.dll') -Destination (Join-Path $root 'assets\Native\GigaBreakLE.dll')
}
Push-Location -LiteralPath $root
try {
    $out = Join-Path $root 'dist\CSNZ_Launcher_1.0.7'
    & dotnet publish (Join-Path $root 'src\CSNZ.Launcher\CSNZ.Launcher.csproj') -c Release --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -o $out --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $out '使用说明.md')
    Write-Host ('Ready: ' + (Join-Path $out 'CSNZ登录器.exe'))
} finally { Pop-Location }
