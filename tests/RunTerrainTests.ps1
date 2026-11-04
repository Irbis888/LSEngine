param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$project=Join-Path $repo 'Chapter 9 Texturing/Try2'
$output=Join-Path $PSScriptRoot ".build/terrain-application-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null
$preserved=@{}
foreach($path in @((Join-Path $project 'Scenes/EditorPlaySnapshot.json'),(Join-Path $project 'imgui.ini'))) {
    $exists=Test-Path -LiteralPath $path
    $bytes=$null
    if($exists){$bytes=[IO.File]::ReadAllBytes($path)}
    $preserved[$path]=@{Exists=$exists;Bytes=$bytes}
}
$process=$null
try {
    $process=Start-Process -FilePath (Join-Path $project "x64/$Configuration/Try2.exe") -ArgumentList '--terrain-smoke' -WorkingDirectory $output -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
    $null=$process.Handle
    if(!$process.WaitForExit(180000)){throw 'Terrain application test timed out'}
    $process.WaitForExit()
    $stdout=Get-Content (Join-Path $output 'stdout.log') -Raw
    $stderr=Get-Content (Join-Path $output 'stderr.log') -Raw
    if($process.ExitCode -ne 0 -or $stdout -notmatch 'TERRAIN_SMOKE_PASS'){throw "Terrain test failed: $stdout`n$stderr"}
    Write-Output $stdout
} finally {
    if($process -and !$process.HasExited){$process.Kill();$process.WaitForExit()}
    foreach($path in $preserved.Keys){
        if($preserved[$path].Exists){[IO.File]::WriteAllBytes($path,$preserved[$path].Bytes)}
        elseif(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
    }
}
