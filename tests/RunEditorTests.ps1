param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $PSScriptRoot ".build/docked-editor-$Configuration"
New-Item -ItemType Directory -Force (Join-Path $project 'Scenes') | Out-Null
@{
    formatVersion = 1
    name = 'Editor Validation'
    startupScene = 'Scenes/Viewport.json'
} | ConvertTo-Json | Set-Content (Join-Path $project 'Editor.lsproject') -Encoding utf8NoBOM
@'
mtllib Submeshes.mtl
v -1 -1 0
v -1 1 0
v 0 1 0
v 0 -1 0
v 1 1 0
v 1 -1 0
vn 0 0 1
usemtl Left
f 1//1 3//1 2//1
f 1//1 4//1 3//1
usemtl Right
f 4//1 5//1 3//1
f 4//1 6//1 5//1
'@ | Set-Content (Join-Path $project 'Submeshes.obj') -Encoding ascii
@'
newmtl Left
Kd 0.7 0.3 0.5
newmtl Right
Kd 0.4 0.6 0.8
'@ | Set-Content (Join-Path $project 'Submeshes.mtl') -Encoding ascii
$entities = @(
    @{ tag = 'Camera'; transform = @{ position = @(0, 0, -12); rotation = @(0, 0, 0); scale = @(1, 1, 1) }; camera = @{ fov = 1.05; nearZ = 0.1; farZ = 500; aspectRatio = 1.5 } },
    @{ tag = 'Sun'; directionalLight = @{ direction = @(-0.6, -0.8, 0.3); intensity = 1.5; color = @(1, 1, 1) } },
    @{ tag = 'Ближняя сфера'; transform = @{ position = @(0, 0, 0); scale = @(4, 4, 4) }; mesh = @{ source = 'primitive'; primitive = 'sphere'; material = @{ color = @(0.7, 0.3, 0.1) } } },
    @{ tag = 'Дальняя сфера'; transform = @{ position = @(0, 0, 5); scale = @(6, 6, 6) }; mesh = @{ source = 'primitive'; primitive = 'sphere'; material = @{ color = @(0.1, 0.3, 0.7) } } },
    @{ tag = 'Submeshes'; transform = @{ position = @(-4, 0, 1); scale = @(1, 1, 1) }; mesh = @{ source = 'model'; path = 'Submeshes.obj' } },
    @{ tag = 'Terrain'; transform = @{ position = @(-20, 0, -5); scale = @(1, 1, 1) }; terrain = @{ source = 'flat'; samplesX = 129; samplesZ = 129; sizeX = 40; sizeZ = 40; heightOffset = -4; chunkCells = 32; lodCount = 3; layerCount = 1 } },
    @{ tag = 'Куб'; transform = @{ position = @(4, 0, 1); scale = @(2, 2, 2) }; mesh = @{ source = 'primitive'; primitive = 'cube'; material = @{ color = @(0.1, 0.7, 0.3) } } }
)
@{ version = 1; name = 'Viewport validation'; entities = $entities } | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $project 'Scenes/Viewport.json') -Encoding utf8NoBOM
$executable = Join-Path $repo "Chapter 9 Texturing/Try2/x64/$Configuration/Try2.exe"
$process = Start-Process -FilePath $executable -ArgumentList @('--project', ('"' + (Join-Path $project 'Editor.lsproject') + '"'), '--editor-smoke') -WorkingDirectory $project -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $project 'stdout.log') -RedirectStandardError (Join-Path $project 'stderr.log')
try {
    if (!$process.WaitForExit(150000)) { throw 'Editor validation process timed out' }
    $process.WaitForExit()
    $stdout = Get-Content (Join-Path $project 'stdout.log') -Raw
    $stderr = Get-Content (Join-Path $project 'stderr.log') -Raw
    if ($process.ExitCode -ne 0 -or $stdout -notmatch 'EDITOR_SMOKE_PASS') { throw "Editor validation failed: $stdout`n$stderr" }
    Write-Output $stdout
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
