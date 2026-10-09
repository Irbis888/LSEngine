param(
    [string]$BaselineExecutable = (Join-Path $PSScriptRoot '.build/loading-baseline/TextureLoadingTests.exe'),
    [int]$Repeats = 3,
    [double[]]$UploadMilliseconds = @(4, 8, 12),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '.build/loading-comparison')
)
$ErrorActionPreference = 'Stop'
if ($Repeats -lt 1) { throw 'Repeats must be positive.' }
$repoPath = Split-Path $PSScriptRoot -Parent
$currentExecutable = Join-Path $PSScriptRoot '.build/texture-tests/TextureLoadingTests.exe'
$fixturesPath = Join-Path $PSScriptRoot '.build/texture-tests/fixtures'
$outputPath = $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$outputPath = (Resolve-Path -LiteralPath $outputPath).Path
if (!(Test-Path -LiteralPath $BaselineExecutable) -or !(Test-Path -LiteralPath $currentExecutable)) {
    throw 'The preserved baseline and current Debug texture-test executables are required. Build with RunTextureTests.ps1 first.'
}
$baselinePath = (Resolve-Path -LiteralPath $BaselineExecutable).Path
$savedPath = $env:PATH
$savedBudget = $env:LSE_TEST_UPLOAD_MS
$env:PATH = (Join-Path $repoPath 'Chapter 9 Texturing/Try2/dlls') + ';' + $savedPath
$rows = [System.Collections.Generic.List[object]]::new()
# Explicitly warm the same scene and DDS files before every process. Do not clear OS caches.
$warmFiles = @(Join-Path $repoPath 'Chapter 9 Texturing/Try2/Scenes/TextureStreaming1000.json') +
    @(Get-ChildItem -LiteralPath (Join-Path $repoPath 'Textures/StreamingStress') -Filter '*.dds' | ForEach-Object FullName)
Push-Location (Join-Path $repoPath 'Chapter 9 Texturing/Try2')
try {
    $cases = @([pscustomobject]@{ Name = 'before'; Exe = $baselinePath; Budget = $null })
    foreach ($budget in $UploadMilliseconds) {
        $cases += [pscustomobject]@{ Name = "after-$budget-ms"; Exe = $currentExecutable; Budget = $budget }
    }
    foreach ($case in $cases) {
        for ($run = 1; $run -le $Repeats; ++$run) {
            foreach ($file in $warmFiles) { $null = [System.IO.File]::ReadAllBytes($file) }
            $env:LSE_TEST_UPLOAD_MS = if ($null -eq $case.Budget) { $null } else {
                $case.Budget.ToString([System.Globalization.CultureInfo]::InvariantCulture)
            }
            $logPath = Join-Path $outputPath "$($case.Name)-$run.log"
            & $case.Exe $repoPath $fixturesPath *> $logPath
            if ($LASTEXITCODE -ne 0) { throw "Loading test failed: $logPath" }
            $log = Get-Content -LiteralPath $logPath -Raw
            $ready = [regex]::Match($log, 'BENCHMARK_ALL_TEXTURES_READY elapsed_ms=([0-9.]+)')
            $frame = [regex]::Match($log, 'max_frame_ms=([0-9.]+)')
            $objects = [regex]::Match($log, 'objects_ready_ms=([0-9.]+)')
            if (!$ready.Success -or !$frame.Success) { throw "Missing benchmark measurements: $logPath" }
            $row = [pscustomobject]@{
                Case = $case.Name; Run = $run
                AllReadyMilliseconds = [double]::Parse($ready.Groups[1].Value, [cultureinfo]::InvariantCulture)
                MaxFrameMilliseconds = [double]::Parse($frame.Groups[1].Value, [cultureinfo]::InvariantCulture)
                ObjectsReadyMilliseconds = if ($objects.Success) { [double]::Parse($objects.Groups[1].Value, [cultureinfo]::InvariantCulture) } else { $null }
                Log = $logPath
            }
            $rows.Add($row)
            $row | Format-Table -AutoSize
        }
    }
} finally {
    Pop-Location
    $env:PATH = $savedPath
    $env:LSE_TEST_UPLOAD_MS = $savedBudget
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -LiteralPath (Join-Path $outputPath 'results.csv')
$rows | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $outputPath 'results.json')
