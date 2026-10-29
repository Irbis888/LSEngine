param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo 'Chapter 9 Texturing/Try2'
$output = Join-Path $PSScriptRoot ".build/ball-game-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null
$process = Start-Process -FilePath (Join-Path $project "x64/$Configuration/Try2.exe") `
    -ArgumentList '--game-smoke','Games/BallField/game.json' -WorkingDirectory $output -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
try {
    if (!$process.WaitForExit(45000)) { throw 'Game smoke timed out' }
    $process.WaitForExit()
    $stdout = Get-Content -LiteralPath (Join-Path $output 'stdout.log') -Raw
    $stderr = Get-Content -LiteralPath (Join-Path $output 'stderr.log') -Raw
    if ($process.ExitCode -ne 0 -or $stdout -notmatch 'GAME_SMOKE_PASS') { throw "Game failed: $stdout`n$stderr" }
    Write-Output "PASS: $Configuration standalone field game, 840 fixed steps, character pushes ball, borders contain bodies, camera fixed, editor disabled"
    Write-Output (($stdout -split "`n" | Where-Object { $_ -match 'GAME_SMOKE_PASS' }) -join "`n")
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
