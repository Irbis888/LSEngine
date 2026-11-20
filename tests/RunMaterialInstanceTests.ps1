$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build\material-instance-tests'
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
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $repo 'external\enkiTS\enkiTS.vcxproj') /nologo /verbosity:minimal /p:Configuration=Debug /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'enkiTS library did not build.' }
& $msbuild (Join-Path $repo 'external/lua/Lua.vcxproj') /nologo /verbosity:minimal /p:Configuration=Debug /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Lua library did not build.' }
$engineSources = @('Engine.cpp', 'Project.cpp', 'AssetPaths.cpp', 'ScriptRuntime.cpp', 'ScriptSystem.cpp', 'SceneSerializer.cpp',
    'Terrain.cpp', 'TerrainSystem.cpp', 'D3DTerrain.cpp', 'D3DEditorViewport.cpp', 'D3DSurfaceMaterials.cpp', 'CameraControllerSystem.cpp',
    'PhysicsSystem.cpp', 'SceneFactory.cpp', 'DemoScene.cpp', 'ResourceManager.cpp', 'ImageIO.cpp', 'JobSystem.cpp',
    'D3DRenderAdapter.cpp', 'd3dUtils.cpp', 'FrameRes.cpp', 'Commons.cpp') | ForEach-Object { Join-Path $project $_ }
$compilerArgs = @('/nologo', '/MP4', '/std:c++20', '/permissive', '/EHsc', '/MDd', '/Od', '/Zi', '/W3', '/D_DEBUG', '/DUNICODE', '/D_UNICODE',
    "/I$project", "/I$(Join-Path $repo 'external/lua/lua-5.5.1/src')", "/I$(Join-Path $repo 'IMGUI_works\src')", "/I$(Join-Path $repo 'IMGUI_works/ThirdParty/imgui')", "/I$(Join-Path $repo 'external\enkiTS\src')", "/I$(Join-Path $repo 'Common')", "/I$(Join-Path $repo 'external\tracy\public')",
    "/Fo$output\", "/Fd$output\", "/Fe$output\MaterialInstanceTests.exe",
    (Join-Path $PSScriptRoot 'MaterialInstanceTests.cpp')) + $engineSources + @(
    (Join-Path $repo 'Common\GameTimer.cpp'), (Join-Path $repo 'Common\DDSTextureLoader.cpp'), (Join-Path $repo 'Common\MathHelper.cpp'),
    (Join-Path $repo 'Chapter 9 Texturing\TexColumns\CustomBuffer.cpp'),
    '/link', (Join-Path $repo 'external/lua/.build/x64/Debug/Lua.lib'), (Join-Path $repo 'external\enkiTS\.build\x64\Debug\enkiTS.lib'), (Join-Path $repo 'Libs\assimp-vc143-mt.lib'), 'd3d12.lib', 'dxgi.lib', 'd3dcompiler.lib', 'user32.lib')
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Material instance tests did not compile.' }
$env:PATH = (Join-Path $project 'dlls') + ';' + $env:PATH
Push-Location $project
try {
    & (Join-Path $output 'MaterialInstanceTests.exe') (Join-Path $output 'fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Material instance tests failed.' }
} finally { Pop-Location }
