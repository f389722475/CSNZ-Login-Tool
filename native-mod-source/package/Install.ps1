param(
    [string]$GameRoot,
    [string]$ServerAddress = '127.0.0.1',
    [ValidateRange(1,65535)][int]$Port = 30002,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
try {
    # Windows PowerShell 5.1 must not inherit another PowerShell's module versions.
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -Force
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force
    $package = [IO.Path]::GetFullPath($PSScriptRoot)
    $payload = Join-Path $package 'payload'
    if (-not (Test-Path -LiteralPath (Join-Path $payload 'CSNZ_GigaBreakLE.exe') -PathType Leaf)) { throw 'Missing native payload. Extract the complete deployment ZIP first.' }
    if (@(Get-Process -Name CSOLauncher,CSNZ_LEGuard -ErrorAction SilentlyContinue).Count -gt 0) { throw 'Exit the game and the old JS/Frida LEGuard before configuring this edition.' }
    if (-not $GameRoot) {
        $cursor = $package
        for ($i=0; $i -lt 7 -and $cursor; $i++) {
            if (Test-Path -LiteralPath (Join-Path $cursor 'Bin\CSOLauncher.exe') -PathType Leaf) { $GameRoot=$cursor; break }
            $cursor = Split-Path -Parent $cursor
        }
    }
    if (-not $GameRoot) {
        if ($NonInteractive) { throw 'Provide -GameRoot (the directory containing Bin).' }
        $GameRoot = Read-Host 'Game root, Bin folder, or Bin\CSOLauncher.exe'
    }
    if ($GameRoot) { $GameRoot = $GameRoot.Trim().Trim('"') }
    if ($GameRoot -and (Test-Path -LiteralPath $GameRoot -PathType Leaf) -and [IO.Path]::GetFileName($GameRoot) -ieq 'CSOLauncher.exe') { $GameRoot = Split-Path -Parent $GameRoot }
    if (-not $GameRoot -or -not (Test-Path -LiteralPath $GameRoot -PathType Container)) { throw 'Game directory does not exist.' }
    $GameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameRoot).FullName)
    while ($GameRoot.Length -gt 3 -and $GameRoot.EndsWith('\')) { $GameRoot = $GameRoot.Substring(0,$GameRoot.Length-1) }
    if ([IO.Path]::GetFileName($GameRoot) -ieq 'Bin' -and (Test-Path -LiteralPath (Join-Path $GameRoot 'CSOLauncher.exe') -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $GameRoot 'Bin\CSOLauncher.exe') -PathType Leaf)) {
        $GameRoot = Split-Path -Parent $GameRoot
        Write-Host ('Bin folder detected. Using game root: ' + $GameRoot)
    }
    foreach ($name in @('CSOLauncher.exe','mp.dll','client.dll')) {
        $required = Join-Path $GameRoot ('Bin\'+$name)
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw ('Required game file not found: '+$required+'. Check the selected path; this is not a version mismatch.') }
    }
    if ($GameRoot.IndexOfAny([char[]]"`r`n") -ge 0 -or $ServerAddress -notmatch '^[A-Za-z0-9.\-:\[\]]+$') { throw 'Invalid game path or server address.' }
    # Check the candidate root BEFORE changing any existing configuration.
    $check = New-Object System.Diagnostics.Process
    $check.StartInfo.FileName = Join-Path $payload 'CSNZ_GigaBreakLE.exe'
    $check.StartInfo.Arguments = '--check-root "' + ($GameRoot -replace '(\\+)$','$1$1') + '"'
    $check.StartInfo.WorkingDirectory = $payload
    $check.StartInfo.UseShellExecute = $false
    $check.StartInfo.CreateNoWindow = $true
    $check.StartInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $check.StartInfo.RedirectStandardOutput = $true
    $check.StartInfo.RedirectStandardError = $true
    try {
        if (-not $check.Start()) { throw 'Cannot start the native preflight.' }
        if (-not $check.WaitForExit(30000)) { $check.Kill(); throw 'Native preflight timed out. No game was started.' }
        $output = $check.StandardOutput.ReadToEnd() + $check.StandardError.ReadToEnd()
        Write-Host $output
        if ($check.ExitCode -ne 0) { throw 'Native preflight failed (see the exact file/build error above). Existing native.ini was not changed.' }
    } finally { $check.Dispose() }
    $ini = Join-Path $payload 'native.ini'
    $backup = $null
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        $backup = Join-Path $payload ('native.ini.backup-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fffffff'))
    }
    $content = "[Game]`r`nRoot=$GameRoot`r`nServer=$ServerAddress`r`nPort=$Port`r`n"
    $pending = Join-Path $payload ('native.ini.pending-' + [Guid]::NewGuid().ToString('N'))
    [IO.File]::WriteAllText($pending,$content,[Text.Encoding]::Unicode)
    if ($backup) {
        [IO.File]::Replace($pending,$ini,$backup)
        Write-Host ('Previous configuration preserved: ' + $backup)
    } else { [IO.File]::Move($pending,$ini) }
    Write-Host 'Configured. No original game files were modified. No game was started.' -ForegroundColor Green
    Write-Host 'Start your local server normally, then use Start_Mod.cmd in this package.'
} catch {
    Write-Host ('INSTALL FAILED: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
