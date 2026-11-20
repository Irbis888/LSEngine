# RenderDoc captures "before" (c5601c9) and "after" (this tree) for the same scenes.
# Needs the Release builds from RunRenderBenchmark.ps1 (-BuildOnly is enough) and RenderDoc installed.
# Each run launches Try2 through renderdoccmd; FrameBenchmark.h waits until the scene is loaded,
# warms up, then triggers the capture through the RenderDoc in-application API and quits.
# Output: tests/.build/renderdoc/<revision>-<scene>/capture_frame*.rdc plus thumbnail.png.
param(
    [string[]]$Scenes = @('Scenes/MaterialsBaseline.json', 'Scenes/RenderBenchmark4096.json'),
    [int]$WarmupFrames = 120
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build\renderdoc'
$renderDoc = Join-Path $env:ProgramFiles 'RenderDoc\renderdoccmd.exe'
if (!(Test-Path $renderDoc)) { throw 'RenderDoc is not installed (expected renderdoccmd.exe in Program Files\RenderDoc).' }
$revisions = @(
    @{ Name = 'before'; Root = Join-Path $PSScriptRoot '.build\render-before\Chapter 9 Texturing\Try2' },
    @{ Name = 'after'; Root = Join-Path $repo 'Chapter 9 Texturing\Try2' }
)

foreach ($scene in $Scenes) {
    foreach ($revision in $revisions) {
        $exe = Join-Path $revision.Root 'x64\Release\Try2.exe'
        if (!(Test-Path $exe)) { throw "Missing $exe; run RunRenderBenchmark.ps1 -BuildOnly first." }
        $name = '{0}-{1}' -f $revision.Name, [System.IO.Path]::GetFileNameWithoutExtension($scene)
        $directory = Join-Path $output $name
        if (Test-Path $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
        New-Item -ItemType Directory -Force $directory | Out-Null
        $env:LS_BENCHMARK_CSV = Join-Path $directory 'frames.csv'
        $env:LS_BENCHMARK_WARMUP = "$WarmupFrames"
        $env:LS_BENCHMARK_FRAMES = '30'
        $env:LS_RENDERDOC_CAPTURE = '1'
        try {
            $process = Start-Process -FilePath $renderDoc -ArgumentList @('capture', '-w', '-d', "`"$($revision.Root)`"",
                '-c', "`"$(Join-Path $directory 'capture')`"", "`"$exe`"", '--play', '--no-hot-reload', '--scene', $scene) `
                -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $directory 'renderdoccmd.log') `
                -RedirectStandardError (Join-Path $directory 'renderdoccmd.err')
            # Windows PowerShell 5.1 reports a null ExitCode unless the handle is opened before exit.
            $null = $process.Handle
            if (!$process.WaitForExit(300000)) {
                $process.Kill()
                throw "$name timed out"
            }
        } finally {
            Remove-Item Env:LS_BENCHMARK_CSV, Env:LS_BENCHMARK_WARMUP, Env:LS_BENCHMARK_FRAMES, Env:LS_RENDERDOC_CAPTURE -ErrorAction SilentlyContinue
        }
        $capture = Get-ChildItem $directory -Filter '*.rdc' | Select-Object -First 1
        if (!$capture -or !(Test-Path (Join-Path $directory 'frames.csv'))) {
            throw "$name produced no capture: $(Get-Content (Join-Path $directory 'renderdoccmd.log') -Raw) $(Get-Content (Join-Path $directory 'renderdoccmd.err') -Raw)"
        }
        & $renderDoc thumb -o (Join-Path $directory 'thumbnail.png') $capture.FullName | Out-Null
        Write-Output ('{0}: {1} ({2:N1} MB)' -f $name, $capture.FullName, ($capture.Length / 1MB))
    }
}
