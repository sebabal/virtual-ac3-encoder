<#
.SYNOPSIS
  Removes the "set and forget" autostart: deletes the Startup-folder supervisor, stops the
  engine, and (best effort) removes any leftover Scheduled Task. Optionally deletes the
  staged install folder. No elevation needed for the Startup-folder removal.

.EXAMPLE
  scripts\remove-autostart.ps1
  scripts\remove-autostart.ps1 -DeleteInstall
#>
[CmdletBinding()]
param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder'),
  [switch]$DeleteInstall
)
$ErrorActionPreference = 'Continue'

# 1. Remove the Startup-folder supervisor so it won't relaunch.
$vbsPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'VirtualAc3Encoder.vbs'
if (Test-Path $vbsPath) { Remove-Item $vbsPath -Force; Write-Host "Removed $vbsPath" }

# 2. Stop the supervisor + engine.
Get-CimInstance Win32_Process -Filter "Name='wscript.exe' OR Name='engine.exe'" |
  Where-Object { $_.CommandLine -like "*virtual-ac3-encoder*" -or $_.CommandLine -like "*VirtualAc3Encoder*" } |
  ForEach-Object { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null; Write-Host "Stopped $($_.Name) PID $($_.ProcessId)" }

# 3. Remove any leftover Scheduled Task from earlier versions (needs elevation; ignore if absent).
if (Get-ScheduledTask -TaskName VirtualAc3Encoder -EA SilentlyContinue) {
  try { Unregister-ScheduledTask -TaskName VirtualAc3Encoder -Confirm:$false; Write-Host "Removed scheduled task." }
  catch { Write-Warning "Leftover scheduled task 'VirtualAc3Encoder' exists; remove it elevated: Unregister-ScheduledTask -TaskName VirtualAc3Encoder -Confirm:`$false" }
}

if ($DeleteInstall -and (Test-Path $InstallDir)) {
  Remove-Item $InstallDir -Recurse -Force
  Write-Host "Deleted $InstallDir."
}
Write-Host "Done."
