param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo 'Chapter 9 Texturing/Try2'
$output = Join-Path $PSScriptRoot ".build/lua-application-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null
$snapshot = Join-Path $project 'Scenes/EditorPlaySnapshot.json'
$ini = Join-Path $project 'imgui.ini'
$preserved = @{}
foreach ($path in @($snapshot, $ini)) {
    $exists = Test-Path -LiteralPath $path
    $bytes = $null
    if ($exists) { $bytes = [IO.File]::ReadAllBytes($path) }
    $preserved[$path] = @{ Exists = $exists; Bytes = $bytes }
}
$process = $null
try {
    $process = Start-Process -FilePath (Join-Path $project "x64/$Configuration/Try2.exe") `
        -ArgumentList '--lua-smoke' -WorkingDirectory $output -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
    if (!$process.WaitForExit(45000)) { $process.Kill(); throw 'Actual Lua application smoke timed out.' }
    $process.WaitForExit()
    $stdout = Get-Content -LiteralPath (Join-Path $output 'stdout.log') -Raw
    $stderr = Get-Content -LiteralPath (Join-Path $output 'stderr.log') -Raw
    if ($process.ExitCode -ne 0 -or $stdout -notmatch 'APP_LUA_SMOKE_PASS') {
        throw "Application smoke failed ($($process.ExitCode))`n$stdout`n$stderr"
    }
    Write-Output "PASS: $Configuration actual Application, D3D12, editor handlers, Lua demo, Play/Stop/Play/Stop; unrelated working directory; no external Lua installation"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($path in @($snapshot, $ini)) {
        if ($preserved[$path].Exists) { [IO.File]::WriteAllBytes($path, [byte[]]$preserved[$path].Bytes) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
}
