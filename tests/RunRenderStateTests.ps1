param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$executable = Join-Path $root "Chapter 9 Texturing/Try2/x64/$Configuration/Try2.exe"
$output = Join-Path $PSScriptRoot ".build/render-states-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null

foreach ($mode in @('editor', 'standalone')) {
    $arguments = @('--render-state-smoke')
    if ($mode -eq 'standalone') {
        $arguments += '--play'
    }
    $stdout = Join-Path $output "$mode.log"
    $stderr = Join-Path $output "$mode-errors.log"
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        if (!$process.WaitForExit(45000)) {
            throw "$mode render-state validation timed out"
        }
        $process.WaitForExit()
        $log = Get-Content -LiteralPath $stdout -Raw
        $errors = Get-Content -LiteralPath $stderr -Raw
        if ($process.ExitCode -ne 0 -or $log -notmatch 'RENDER_STATES_(EDITOR|STANDALONE)_PASS') {
            throw "$mode render-state validation failed: $log`n$errors"
        }
        $log
        if ($errors) {
            $errors
        }
    } finally {
        if (!$process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
