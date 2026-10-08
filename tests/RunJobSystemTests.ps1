param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $PSScriptRoot ".build\job-system-$Configuration"
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
$enkiProject = Join-Path $repo 'external\enkiTS\enkiTS.vcxproj'
$enkiLibrary = Join-Path $repo "external\enkiTS\.build\x64\$Configuration\enkiTS.lib"
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild $enkiProject /nologo /verbosity:minimal "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'enkiTS library did not build.' }
$compilerArgs = @('/nologo', '/std:c++20', '/EHsc', '/W4', "/I$project",
    "/I$(Join-Path $repo 'external\enkiTS\src')", "/Fo$output\", "/Fd$output\",
    "/Fe$output\JobSystemTests.exe", (Join-Path $PSScriptRoot 'JobSystemTests.cpp'),
    (Join-Path $project 'JobSystem.cpp'))
if ($Configuration -eq 'Debug') { $compilerArgs += @('/MDd', '/Od', '/Zi', '/D_DEBUG') }
else { $compilerArgs += @('/MD', '/O2', '/DNDEBUG') }
$compilerArgs += @('/link', $enkiLibrary)
& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'JobSystem tests did not compile.' }
# A bounded runner makes scheduler deadlocks fail the test instead of hanging.
$process = Start-Process -FilePath (Join-Path $output 'JobSystemTests.exe') -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
if (!$process.WaitForExit(30000)) {
    $process.Kill()
    throw 'JobSystem tests timed out.'
}
$process.WaitForExit()
Get-Content -LiteralPath (Join-Path $output 'stdout.log'), (Join-Path $output 'stderr.log')
if ($process.ExitCode -ne 0) { throw "JobSystem tests failed: $($process.ExitCode)" }
