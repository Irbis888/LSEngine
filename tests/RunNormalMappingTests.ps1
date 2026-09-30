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
$compilerArgs = @('/nologo', '/std:c++20', '/EHsc', '/O2', '/W3', '/DUNICODE', '/D_UNICODE',
    "/I$project", "/I$(Join-Path $repo 'Common')",
    "/I$(Join-Path $repo 'external\tracy\public')", "/I$(Join-Path $repo 'external\enkiTS\src')",
    "/Fo$output\", "/Fe$output\NormalMappingTests.exe",
    (Join-Path $PSScriptRoot 'NormalMappingTests.cpp'),
    (Join-Path $project 'ResourceManager.cpp'),
    (Join-Path $repo 'Libs\assimp-vc143-mt.lib'))
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Normal mapping tests did not compile.' }
$env:PATH = (Join-Path $project 'dlls') + ';' + $env:PATH
& (Join-Path $output 'NormalMappingTests.exe') (Join-Path $repo 'Common\negr.obj')
if ($LASTEXITCODE -ne 0) { throw 'Normal mapping tests failed.' }
foreach ($shader in @('Default', 'GeometryPass')) {
    foreach ($entry in @('VS', 'PS')) {
        & fxc.exe /nologo /T ($entry.ToLower() + '_5_1') /E $entry /Fo (Join-Path $output "$shader-$entry.cso") (Join-Path $project "Shaders\$shader.hlsl")
        if ($LASTEXITCODE -ne 0) { throw "$shader $entry did not compile." }
    }
}
& fxc.exe /nologo /T vs_5_1 /E VSTerrain /Fo (Join-Path $output 'GeometryPass-VSTerrain.cso') (Join-Path $project 'Shaders\GeometryPass.hlsl')
if ($LASTEXITCODE -ne 0) { throw 'Terrain vertex shader did not compile.' }
