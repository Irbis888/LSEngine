param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$environmentLines = & cmd.exe /d /s /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the C++ compiler environment.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$project = Join-Path $repo 'Chapter 9 Texturing\Try2'
$compilerArgs = @('/nologo', '/std:c++20', '/EHsc', '/O2', '/W3',
    "/I$project", "/I$(Join-Path $repo 'Common')",
    "/I$(Join-Path $repo 'external\tracy\public')",
    "/Fo$output\", "/Fe$output\PhysicsBroadPhaseTests.exe",
    (Join-Path $PSScriptRoot 'PhysicsBroadPhaseTests.cpp'),
    (Join-Path $PSScriptRoot 'PhysicsNarrowPhaseTests.cpp'),
    (Join-Path $project 'PhysicsSystem.cpp'),
    (Join-Path $repo 'Common\GameTimer.cpp'))
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Physics tests did not compile.' }
& (Join-Path $output 'PhysicsBroadPhaseTests.exe') (Join-Path $project 'Scenes\PhysicsStress256.json') (Join-Path $project 'Scenes\PhysicsMixed512.json')
if ($LASTEXITCODE -ne 0) { throw 'Physics tests failed.' }


$sceneCompilerArgs = @('/nologo', '/std:c++20', '/EHsc', '/O2', '/W3',
    "/I$project", "/I$(Join-Path $repo 'Common')",
    "/I$(Join-Path $repo 'external\tracy\public')",
    "/Fo$output\", "/Fe$output\SceneLoadingTests.exe",
    (Join-Path $PSScriptRoot 'SceneLoadingTests.cpp'),
    (Join-Path $project 'SceneSerializer.cpp'),
    (Join-Path $project 'ResourceManager.cpp'),
    (Join-Path $repo 'Libs\assimp-vc143-mt.lib'))
& cl.exe @sceneCompilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Scene loader tests did not compile.' }
$env:PATH = (Join-Path $project 'dlls') + ';' + $env:PATH
& (Join-Path $output 'SceneLoadingTests.exe') (Join-Path $project 'Scenes\PhysicsMixed512.json') (Join-Path $output 'MixedSceneSnapshot.json')
if ($LASTEXITCODE -ne 0) { throw 'Scene loader tests failed.' }

