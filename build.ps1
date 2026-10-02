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
# Class Awakening is authored in the sibling engineering project. Packaged source
# also includes it as native-awakening-source for a self-contained rebuild.
$awakening = Join-Path $root 'native-awakening-source'
if (-not (Test-Path -LiteralPath (Join-Path $awakening 'build.cmd'))) { $awakening = Join-Path $root '..\awakening\native' }
if ($RebuildNative -or -not (Test-Path -LiteralPath (Join-Path $root 'assets\Native\AwakeningHost.exe'))) {
    & (Join-Path $awakening 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'Class Awakening native build failed.' }
    foreach ($name in @('AwakeningHost.exe','ClassAwakening.Server.dll','AwakeningObserverHost.exe','ClassAwakening.Observer.dll')) {
        Copy-Item -LiteralPath (Join-Path $awakening ('build\bin\'+$name)) -Destination (Join-Path $root ('assets\Native\'+$name))
    }
}
Push-Location -LiteralPath $root
try {
    $out = Join-Path $root 'dist\CSNZ_Launcher_1.0.8'
    & dotnet publish (Join-Path $root 'src\CSNZ.Launcher\CSNZ.Launcher.csproj') -c Release --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -o $out --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
    foreach ($name in @('README.md','README.en.md')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $out $name)
    }
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $out '使用说明.md')
    Write-Host ('Ready: ' + (Join-Path $out 'CSNZ登录器.exe'))
} finally { Pop-Location }
