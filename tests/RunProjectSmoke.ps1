param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
# End-to-end project workflow through the real editor process (see PROJECTS.md):
# 1. --new-project in a folder outside the repository (spaces + Cyrillic in the path),
#    File > New Scene, PNG from outside the project imported into Assets/Textures, Save,
#    Set as Startup Scene, reload, Play in a separate window, Stop.
# 2. --project reopens it: startup scene + texture, Play window closed by the user.
# 3. --play alone starts the game window for the project's startup scene.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$engine = Join-Path $repo 'Chapter 9 Texturing/Try2'
$exe = Join-Path $engine "x64/$Configuration/Try2.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Build $Configuration x64 first." }
$output = Join-Path $PSScriptRoot ".build/project-smoke-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null
$base = Join-Path ([IO.Path]::GetTempPath()) ("LSEngine project smoke " + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$projectDir = Join-Path $base 'Проект Smoke'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ProjectSmokeWindows {
    public delegate bool Callback(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static string Text(IntPtr h) { var b = new StringBuilder(2048); GetWindowText(h,b,b.Capacity); return b.ToString(); }
}
'@
$ini = Join-Path $engine 'imgui.ini'
$iniExists = Test-Path -LiteralPath $ini
$iniBytes = $null
if ($iniExists) { $iniBytes = [IO.File]::ReadAllBytes($ini) }

function Invoke-Editor([string]$name, [string[]]$arguments, [string]$pass) {
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $output -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $output "$name.out.log") -RedirectStandardError (Join-Path $output "$name.err.log")
    $null = $process.Handle # Windows PowerShell 5.1 reports no ExitCode unless the handle is opened before exit.
    try {
        if (!$process.WaitForExit(120000)) { throw "$name timed out" }
        $process.WaitForExit()
        $stdout = Get-Content -LiteralPath (Join-Path $output "$name.out.log") -Raw -Encoding UTF8
        $stderr = Get-Content -LiteralPath (Join-Path $output "$name.err.log") -Raw -Encoding UTF8
        if ($process.ExitCode -ne 0 -or $stdout -notmatch $pass) { throw "$name failed ($($process.ExitCode))`n$stdout`n$stderr" }
        Write-Output (($stdout -split "`n" | Where-Object { $_ -match $pass }) -join "`n").Trim()
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

try {
    Invoke-Editor 'create' @('--new-project', "`"$projectDir`"", '--name', '"Smoke Проект"', '--project-smoke', 'create') 'PROJECT_SMOKE_PASS'

    $projectFile = Join-Path $projectDir 'Smoke Проект.lsproject'
    if (!(Test-Path -LiteralPath $projectFile)) { throw 'Project file was not created.' }
    foreach ($folder in 'Scenes','Assets','Scripts','Prefabs','Assets/Textures') {
        if (!(Test-Path -LiteralPath (Join-Path $projectDir $folder) -PathType Container)) { throw "Missing $folder" }
    }
    $project = Get-Content -LiteralPath $projectFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($project.formatVersion -ne 1 -or $project.startupScene -ne 'Scenes/Textured.json') { throw "Unexpected project file: $($project | ConvertTo-Json)" }
    $scene = Get-Content -LiteralPath (Join-Path $projectDir 'Scenes/Textured.json') -Raw -Encoding UTF8
    if ($scene -notmatch '"albedo": "Assets/Textures/external-checker.png"') { throw 'Scene does not reference the imported PNG relatively.' }
    if (!(Test-Path -LiteralPath (Join-Path $projectDir 'Assets/Textures/external-checker.png'))) { throw 'PNG was not imported.' }
    Write-Output "PASS: project file, standard folders, relative texture path in Scenes/Textured.json"

    Invoke-Editor 'reopen' @('--project', "`"$projectDir`"", '--project-smoke', 'reopen') 'PROJECT_SMOKE_PASS'

    # The game process on its own, as the editor starts it.
    $process = Start-Process -FilePath $exe -ArgumentList @('--play', '--project', "`"$projectFile`"") -WorkingDirectory $output -PassThru `
        -RedirectStandardOutput (Join-Path $output 'play.out.log') -RedirectStandardError (Join-Path $output 'play.err.log')
    $null = $process.Handle
    try {
        $title = $null
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        while (!$title -and [DateTime]::UtcNow -lt $deadline -and !$process.HasExited) {
            Start-Sleep -Milliseconds 250
            [ProjectSmokeWindows]::EnumWindows({ param($handle, $unused)
                $owner = 0
                [void][ProjectSmokeWindows]::GetWindowThreadProcessId($handle, [ref]$owner)
                $text = [ProjectSmokeWindows]::Text($handle)
                if ($owner -eq $process.Id -and $text -like '*Play: Textured*Esc closes*') { $script:title = $text; $script:window = $handle }
                return $true
            }, [IntPtr]::Zero) | Out-Null
        }
        if (!$title) { throw "Play window did not start playing. $(Get-Content -LiteralPath (Join-Path $output 'play.err.log') -Raw)" }
        [void][ProjectSmokeWindows]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        if (!$process.WaitForExit(10000)) { throw 'Play window did not close.' }
        if ($process.ExitCode -ne 0) { throw "Play window exit code $($process.ExitCode)" }
        Write-Output "PASS: --play window '$title', closed normally"
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
} finally {
    if ($iniExists) { [IO.File]::WriteAllBytes($ini, $iniBytes) }
    elseif (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
}
