$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot '.build\texture-tests'
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
$compilerArgs = @('/nologo', '/std:c++20', '/permissive', '/EHsc', '/MDd', '/Od', '/Zi', '/W3', '/D_DEBUG', '/DUNICODE', '/D_UNICODE',
    "/I$project", "/I$(Join-Path $repo 'Common')", "/I$(Join-Path $repo 'external\tracy\public')",
    "/Fo$output\", "/Fd$output\", "/Fe$output\TextureLoadingTests.exe",
    (Join-Path $PSScriptRoot 'TextureLoadingTests.cpp'),
    (Join-Path $project 'ResourceManager.cpp'), (Join-Path $project 'D3DRenderAdapter.cpp'),
    (Join-Path $project 'd3dUtils.cpp'), (Join-Path $project 'FrameRes.cpp'),
    (Join-Path $project 'Commons.cpp'),
    (Join-Path $repo 'Common\DDSTextureLoader.cpp'), (Join-Path $repo 'Common\MathHelper.cpp'),
    (Join-Path $repo 'Chapter 9 Texturing\TexColumns\CustomBuffer.cpp'),
    '/link', (Join-Path $repo 'Libs\assimp-vc143-mt.lib'), 'd3d12.lib', 'dxgi.lib', 'd3dcompiler.lib', 'user32.lib')
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Texture tests did not compile.' }
$env:PATH = (Join-Path $project 'dlls') + ';' + $env:PATH
Push-Location $project
try {
    & (Join-Path $output 'TextureLoadingTests.exe') $repo (Join-Path $output 'fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Texture tests failed.' }
} finally { Pop-Location }
