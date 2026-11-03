<#
.SYNOPSIS
Installs the probe reader (Tools/bench/probe.py) as a scheduled task of this user that starts at
logon and keeps running. It records the adapter's debug probe point to one file per session
(Firmware/RP2040/docs/Debug_Mode.md).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools\debug\install.ps1
powershell -ExecutionPolicy Bypass -File Tools\debug\install.ps1 -Port COM5 -Dir $HOME\logs\probe
powershell -ExecutionPolicy Bypass -File Tools\debug\install.ps1 -Uninstall

.NOTES
Needs Python 3.8+ (the py launcher or python on PATH). pyserial goes in a venv of its own, in
%LOCALAPPDATA%\ogx-probe. Windows 10 and later have the USB serial drivers for FTDI and CH340
readers; a CP210x reader may need Silicon Labs' driver.
#>
param(
    [string]$Port = "auto",
    [string]$Dir = (Join-Path $HOME "ogx-probe-logs"),
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$TaskName = "OGX-Mini probe reader"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Probe = Join-Path $Repo "Tools\bench\probe.py"
$Home_ = Join-Path $env:LOCALAPPDATA "ogx-probe"

if ($Uninstall) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $Home_ -ErrorAction SilentlyContinue
    Write-Host "Removed. The session files in your log folder are kept."
    exit 0
}

if (-not (Test-Path $Probe)) { throw "not found: $Probe (run this from the repository)" }

if (Get-Command py -ErrorAction SilentlyContinue) { $python = "py"; $pyArgs = @("-3") }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $python = "python"; $pyArgs = @() }
else { throw "Python 3 is needed (https://www.python.org/downloads/)" }

Write-Host "Python environment in $Home_"
& $python @pyArgs -m venv (Join-Path $Home_ "venv")
if ($LASTEXITCODE -ne 0) { throw "python -m venv failed" }
$venvPy = Join-Path $Home_ "venv\Scripts\python.exe"
$venvPyw = Join-Path $Home_ "venv\Scripts\pythonw.exe"  # no console window
& $venvPy -m pip install --quiet --disable-pip-version-check --upgrade pyserial
if ($LASTEXITCODE -ne 0) { throw "pip install pyserial failed" }
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$Dir = (Resolve-Path $Dir).Path

$action = New-ScheduledTaskAction -Execute $venvPyw `
    -Argument "-u `"$Probe`" --port $Port --dir `"$Dir`" --quiet" -WorkingDirectory $Dir
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
# No time limit, restarted a minute after it stops, kept on battery.
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings `
    -Description "Records the OGX-Mini adapter's debug probe point (Tools/bench/probe.py)" -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
Write-Host "Installed: the task `"$TaskName`" (Task Scheduler), running now and at every logon."
Write-Host "Session files go to $Dir. Turn on the adapter's Activity log in the web app."
