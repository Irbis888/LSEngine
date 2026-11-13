param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "Chapter 9 Texturing/Try2/x64/$Configuration/Try2.exe"
$output = Join-Path $PSScriptRoot ".build/material-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null

function Invoke-Check([string]$name, [string[]]$arguments, [string]$marker) {
    $stdout = Join-Path $output "$name.log"
    $stderr = Join-Path $output "$name-errors.log"
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        if (!$process.WaitForExit(45000)) { throw "$name timed out" }
        $process.WaitForExit()
        $log = Get-Content -LiteralPath $stdout -Raw
        $errors = Get-Content -LiteralPath $stderr -Raw
        if ($process.ExitCode -ne 0 -or $log -notmatch $marker) {
            throw "$name failed: $log`n$errors"
        }
        $log
        if ($errors) { $errors }
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

Invoke-Check 'editor' @('--material-smoke') 'MATERIAL_SMOKE_PASS'
Invoke-Check 'standalone' @('--play', '--material-smoke') 'MATERIAL_STANDALONE_PASS'
Invoke-Check 'baseline' @('--scene', 'Scenes/MaterialsBaseline.json', '--editor-preview') 'EDITOR_PREVIEW_PASS'
Invoke-Check 'terrain' @('--scene', 'Scenes/TerrainDemo.json', '--editor-preview') 'EDITOR_PREVIEW_PASS'
