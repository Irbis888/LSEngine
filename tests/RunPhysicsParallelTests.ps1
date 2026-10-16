param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build/physics-parallel'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$environmentLines = & cmd.exe /d /s /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize C++ tools.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$project = Join-Path $repo 'Chapter 9 Texturing/Try2'
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
& $msbuild (Join-Path $repo 'external/enkiTS/enkiTS.vcxproj') /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'enkiTS did not build.' }
$compilerArgs = @('/nologo', '/MD', '/std:c++20', '/permissive', '/EHsc', '/O2', '/W3', '/DNDEBUG',
    "/I$project", "/I$(Join-Path $repo 'Common')", "/I$(Join-Path $repo 'external/enkiTS/src')",
    "/I$(Join-Path $repo 'external/tracy/public')", "/Fo$output\", "/Fe$output\PhysicsParallelTests.exe",
    (Join-Path $PSScriptRoot 'PhysicsParallelTests.cpp'), (Join-Path $project 'PhysicsSystem.cpp'),
    (Join-Path $project 'SceneSerializer.cpp'), (Join-Path $project 'ResourceManager.cpp'),
    (Join-Path $project 'JobSystem.cpp'), (Join-Path $repo 'Common/GameTimer.cpp'),
    (Join-Path $repo 'Common/DDSTextureLoader.cpp'), '/link',
    (Join-Path $repo 'external/enkiTS/.build/x64/Release/enkiTS.lib'),
    (Join-Path $repo 'Libs/assimp-vc143-mt.lib'), 'd3d12.lib', 'dxgi.lib')
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Parallel physics tests did not compile.' }
$env:PATH = (Join-Path $project 'dlls') + ';' + $env:PATH
& (Join-Path $output 'PhysicsParallelTests.exe') $repo $output
if ($LASTEXITCODE -ne 0) { throw 'Parallel physics tests failed.' }
