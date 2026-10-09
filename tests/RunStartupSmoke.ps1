param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$projectPath = Join-Path $repoPath 'Chapter 9 Texturing/Try2'
$exePath = Join-Path $projectPath "x64/$Configuration/Try2.exe"
$logPath = Join-Path $PSScriptRoot ".build/startup-$Configuration"
New-Item -ItemType Directory -Force -Path $logPath | Out-Null
if (!(Test-Path -LiteralPath $exePath)) { throw "Build $Configuration x64 first." }
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class StartupWindows {
    public delegate bool Callback(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static string Text(IntPtr h) { var b = new StringBuilder(2048); GetWindowText(h,b,b.Capacity); return b.ToString(); }
}
'@
# Keep the user's editor layout intact after the hidden app shuts down normally.
$iniPath = Join-Path $projectPath 'imgui.ini'
$iniExists = Test-Path -LiteralPath $iniPath
$iniBytes = $null
if ($iniExists) { $iniBytes = [IO.File]::ReadAllBytes($iniPath) }
try {
    foreach ($case in @(
        [pscustomobject]@{ Name='exe-directory'; Directory=(Split-Path $exePath) },
        [pscustomobject]@{ Name='project-directory'; Directory=$projectPath },
        [pscustomobject]@{ Name='unrelated-directory'; Directory=$logPath }
    )) {
        $process = Start-Process -FilePath $exePath -WorkingDirectory $case.Directory -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logPath "$($case.Name).out.log") `
            -RedirectStandardError (Join-Path $logPath "$($case.Name).err.log")
        try {
            if ($process.WaitForExit(2000)) { throw "$($case.Name): exited prematurely with code $($process.ExitCode)." }
            $windows = [System.Collections.Generic.List[object]]::new()
            [StartupWindows]::EnumWindows({ param($handle,$unused)
                $ownerId = 0
                [void][StartupWindows]::GetWindowThreadProcessId($handle,[ref]$ownerId)
                if ($ownerId -eq $process.Id) {
                    $windows.Add([pscustomobject]@{Handle=$handle; Title=[StartupWindows]::Text($handle)})
                }
                return $true
            }, [IntPtr]::Zero) | Out-Null
            $failed = @($windows | Where-Object Title -in @('HR Failed','Startup failed'))
            if ($failed.Count) { throw "$($case.Name): startup error dialog." }
            $mainWindow = @($windows | Where-Object Title -eq 'LS Engine')
            if (!$mainWindow.Count) { throw "$($case.Name): engine window was not created." }
            [void][StartupWindows]::PostMessage($mainWindow[0].Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            if (!$process.WaitForExit(5000)) { throw "$($case.Name): normal shutdown did not finish." }
            if ($process.ExitCode -ne 0) { throw "$($case.Name): shutdown code $($process.ExitCode)." }
            $log = Get-Content -LiteralPath (Join-Path $logPath "$($case.Name).out.log") -Raw
            if ($log -notmatch 'Loaded scene from Scenes[/\\]DemoScene.json') {
                throw "$($case.Name): startup scene did not load. See $logPath."
            }
            Write-Output "PASS: $Configuration $($case.Name), shaders/scene loaded, normal shutdown"
        } finally {
            if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
            $process.Dispose()
        }
    }
} finally {
    if ($iniExists) { [IO.File]::WriteAllBytes($iniPath, $iniBytes) }
    elseif (Test-Path -LiteralPath $iniPath) { Remove-Item -LiteralPath $iniPath }
}
