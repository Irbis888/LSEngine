# Render cost of the material system: "before" (c5601c9, the last revision without it) vs "after" (this tree).
# Methodology (lecture 2): Release builds, warmup after the scene is ready, N runs per configuration in
# alternating order, per-run median/p95/p99, reported as the median across runs.
#
#   .\tests\RunRenderBenchmark.ps1                 build both revisions, run, write the report
#   .\tests\RunRenderBenchmark.ps1 -SkipBuild      reuse the binaries from the previous call
#   .\tests\RunRenderBenchmark.ps1 -BuildOnly      prepare and build both revisions, run nothing
#
# Both builds get the same measurement patch (FrameBenchmark.h): frame interval, CPU frame
# (frame start to Present returned) and CPU draw (Engine::Draw: render system, material binding,
# command recording). Results: tests/.build/render-benchmark/results.csv and summary.md.
param(
    [int]$Runs = 5,
    [int]$WarmupFrames = 300,
    [int]$MeasuredFrames = 1500,
    [string]$BeforeRevision = 'c5601c9',
    [switch]$SkipBuild,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build\render-benchmark'
$beforeTree = Join-Path $PSScriptRoot '.build\render-before'
New-Item -ItemType Directory -Force $output | Out-Null
$culture = [System.Globalization.CultureInfo]::InvariantCulture
# results.csv must not depend on the machine's decimal separator.
[System.Threading.Thread]::CurrentThread.CurrentCulture = $culture

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools are required.' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (!$git) {
    $git = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe'
}
if (!(Test-Path $git)) { throw 'git is required to check out the "before" revision.' }

function Invoke-Build([string]$root) {
    foreach ($project in @('external\enkiTS\enkiTS.vcxproj', 'external\lua\Lua.vcxproj', 'Chapter 9 Texturing\Try2\Try2.vcxproj')) {
        & $msbuild (Join-Path $root $project) /nologo /verbosity:minimal /m /p:Configuration=Release /p:Platform=x64
        if ($LASTEXITCODE -ne 0) { throw "Release build failed: $root\$project" }
    }
}

# Replaces one exact fragment; a missing or repeated anchor means the patch no longer fits the revision.
function Set-Patch([string]$file, [string]$anchor, [string]$replacement) {
    $text = [System.IO.File]::ReadAllText($file)
    $count = ([regex]::Matches($text, [regex]::Escape($anchor))).Count
    if ($count -ne 1) { throw "Patch anchor found $count times in $file : $anchor" }
    [System.IO.File]::WriteAllText($file, $text.Replace($anchor, $replacement), (New-Object System.Text.UTF8Encoding($true)))
}

$afterProject = Join-Path $repo 'Chapter 9 Texturing\Try2'
$beforeProject = Join-Path $beforeTree 'Chapter 9 Texturing\Try2'
if (!$SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'GenerateRenderBenchmarkScenes.ps1')
    if (Test-Path $beforeTree) {
        & $git -C $repo worktree remove --force $beforeTree
    }
    & $git -C $repo worktree add --detach $beforeTree $BeforeRevision
    if ($LASTEXITCODE -ne 0) { throw "Cannot check out $BeforeRevision" }
    # Same Tracy submodule commit in both revisions: reuse the checked-out copy.
    $tracy = Join-Path $beforeTree 'external\tracy'
    if (Test-Path $tracy) { Remove-Item -LiteralPath $tracy -Recurse -Force }
    New-Item -ItemType Junction -Path $tracy -Target (Join-Path $repo 'external\tracy') | Out-Null

    Copy-Item (Join-Path $afterProject 'FrameBenchmark.h') $beforeProject
    $renderDocHeader = Join-Path $beforeTree 'Common\ThirdParty\renderdoc'
    New-Item -ItemType Directory -Force $renderDocHeader | Out-Null
    Copy-Item (Join-Path $repo 'Common\ThirdParty\renderdoc\renderdoc_app.h') $renderDocHeader -Force
    Set-Patch (Join-Path $beforeProject 'Application.cpp') '#include "TextureBenchmark.h"' "#include `"TextureBenchmark.h`"`r`n#include `"FrameBenchmark.h`""
    Set-Patch (Join-Path $beforeProject 'Application.cpp') "ZoneScopedN(`"Engine frame CPU`");" "ZoneScopedN(`"Engine frame CPU`");`r`n`t`t`tFrameBenchmark::Instance().FrameBoundary();"
    Set-Patch (Join-Path $beforeProject 'Application.cpp') 'mRenderAdapter->EndFrame();' "mRenderAdapter->EndFrame();`r`n`t`t`t`tFrameBenchmark::Instance().EndCpuFrame();"
    Set-Patch (Join-Path $beforeProject 'Try2.cpp') '#include "Application.h"' "#include `"Application.h`"`r`n#include `"FrameBenchmark.h`""
    Set-Patch (Join-Path $beforeProject 'Try2.cpp') 'mEngine->Draw(context);' ("FrameBenchmark& benchmark = FrameBenchmark::Instance();`r`n`tbenchmark.SetSceneReady(!mEngine->IsSceneLoading() && !mEngine->GetResources().HasPendingLoads());" +
        "`r`n`tbenchmark.BeginDraw();`r`n`tmEngine->Draw(context);`r`n`tbenchmark.EndDraw();")

    # Legacy-format data only: the "before" engine reads color, roughness, albedo and normal.
    foreach ($relative in @('Scenes\RenderBenchmark4096.json', 'Scenes\MaterialsBaseline.json')) {
        Copy-Item (Join-Path $afterProject $relative) (Join-Path $beforeProject $relative) -Force
    }
    $beforeAssets = Join-Path $beforeProject 'Assets\MaterialDemo'
    New-Item -ItemType Directory -Force $beforeAssets | Out-Null
    Copy-Item (Join-Path $afterProject 'Assets\MaterialDemo\*') $beforeAssets -Recurse -Force

    Invoke-Build $repo
    Invoke-Build $beforeTree
}
if ($BuildOnly) {
    Write-Output "Built: $afterProject\x64\Release\Try2.exe and $beforeProject\x64\Release\Try2.exe"
    return
}

$configurations = @(
    @{ Name = 'before-4096-inline'; Revision = 'before'; Root = $beforeProject; Scene = 'Scenes/RenderBenchmark4096.json' },
    @{ Name = 'after-4096-inline'; Revision = 'after'; Root = $afterProject; Scene = 'Scenes/RenderBenchmark4096.json' },
    @{ Name = 'after-4096-instances'; Revision = 'after'; Root = $afterProject; Scene = 'Scenes/RenderBenchmark4096Instances.json' },
    @{ Name = 'before-baseline'; Revision = 'before'; Root = $beforeProject; Scene = 'Scenes/MaterialsBaseline.json' },
    @{ Name = 'after-baseline'; Revision = 'after'; Root = $afterProject; Scene = 'Scenes/MaterialsBaseline.json' }
)

function Get-Percentile([double[]]$sorted, [double]$fraction) {
    $position = [Math]::Ceiling($fraction * $sorted.Count) - 1
    return $sorted[[Math]::Max(0, [Math]::Min($sorted.Count - 1, $position))]
}

function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    $middle = [Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2
}

function Invoke-Run($configuration, [int]$run) {
    $exe = Join-Path $configuration.Root 'x64\Release\Try2.exe'
    $csv = Join-Path $output ("{0}-run{1}.csv" -f $configuration.Name, $run)
    $log = Join-Path $output ("{0}-run{1}.log" -f $configuration.Name, $run)
    Remove-Item -LiteralPath $csv -ErrorAction SilentlyContinue
    $env:LS_BENCHMARK_CSV = $csv
    $env:LS_BENCHMARK_WARMUP = "$WarmupFrames"
    $env:LS_BENCHMARK_FRAMES = "$MeasuredFrames"
    try {
        $process = Start-Process -FilePath $exe -ArgumentList @('--play', '--no-hot-reload', '--scene', $configuration.Scene) `
            -WorkingDirectory $configuration.Root -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        # Windows PowerShell 5.1 reports a null ExitCode unless the handle is opened before exit.
        $null = $process.Handle
        if (!$process.WaitForExit(300000)) {
            $process.Kill()
            throw "$($configuration.Name) run $run timed out"
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0 -or !(Test-Path $csv)) {
            throw "$($configuration.Name) run $run failed (exit $($process.ExitCode)): $(Get-Content $log -Raw) $(Get-Content "$log.err" -Raw)"
        }
    } finally {
        Remove-Item Env:LS_BENCHMARK_CSV, Env:LS_BENCHMARK_WARMUP, Env:LS_BENCHMARK_FRAMES -ErrorAction SilentlyContinue
    }
    $rows = Import-Csv $csv
    $result = [ordered]@{ configuration = $configuration.Name; revision = $configuration.Revision; scene = $configuration.Scene; run = $run; frames = $rows.Count }
    foreach ($metric in @('frame_ms', 'cpu_frame_ms', 'draw_ms')) {
        $values = [double[]]($rows | ForEach-Object { [double]::Parse($_.$metric, $culture) })
        $sorted = [double[]]($values | Sort-Object)
        $result["${metric}_median"] = Get-Median $sorted
        $result["${metric}_p95"] = Get-Percentile $sorted 0.95
        $result["${metric}_p99"] = Get-Percentile $sorted 0.99
    }
    return [pscustomobject]$result
}

# Round-robin with the order reversed on every other round, so drift (thermal, power) is spread evenly.
$results = @()
for ($run = 1; $run -le $Runs; ++$run) {
    $order = $configurations
    if ($run % 2 -eq 0) {
        $order = $configurations.Clone()
        [array]::Reverse($order)
    }
    foreach ($configuration in $order) {
        Write-Output ("run {0}/{1}: {2}" -f $run, $Runs, $configuration.Name)
        $results += Invoke-Run $configuration $run
    }
}
$results | Export-Csv (Join-Path $output 'results.csv') -NoTypeInformation -Encoding UTF8

$lines = @('| Configuration | Frame median | Frame p95 | Frame p99 | CPU frame median | CPU frame p99 | CPU draw median | CPU draw p95 | CPU draw p99 |',
    '|---|---:|---:|---:|---:|---:|---:|---:|---:|')
foreach ($configuration in $configurations) {
    $runsOf = $results | Where-Object { $_.configuration -eq $configuration.Name }
    $value = { param($column) (Get-Median ([double[]]($runsOf | ForEach-Object { $_.$column }))).ToString('0.000', $culture) }
    $lines += ('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} |' -f $configuration.Name,
        (& $value 'frame_ms_median'), (& $value 'frame_ms_p95'), (& $value 'frame_ms_p99'),
        (& $value 'cpu_frame_ms_median'), (& $value 'cpu_frame_ms_p99'),
        (& $value 'draw_ms_median'), (& $value 'draw_ms_p95'), (& $value 'draw_ms_p99'))
}
$summary = ("Milliseconds; median across {0} runs of per-run statistics; {1} warmup + {2} measured frames per run.`n`n" -f $Runs, $WarmupFrames, $MeasuredFrames) + ($lines -join "`n")
[System.IO.File]::WriteAllText((Join-Path $output 'summary.md'), $summary)
Write-Output $summary
