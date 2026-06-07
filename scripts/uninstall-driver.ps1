<#
.SYNOPSIS
  Removes the Virtual AC3 Encoder virtual audio device and (optionally) its staged driver
  package. Run elevated (Administrator).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\uninstall-driver.ps1
  powershell -ExecutionPolicy Bypass -File scripts\uninstall-driver.ps1 -DisableTestSigning
#>
[CmdletBinding()]
param(
  [switch]$DisableTestSigning
)
$ErrorActionPreference = 'Continue'

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)) {
  throw "Run this script elevated (Administrator)."
}

$devcon = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Tools' -Recurse -Filter devcon.exe -EA SilentlyContinue |
          Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1 -ExpandProperty FullName
if ($devcon) {
  Write-Host "Removing device ROOT\SimpleAudioSample ..."
  & $devcon remove "ROOT\SimpleAudioSample"
} else {
  Write-Warning "devcon.exe not found; remove the device via Device Manager (Sound, video and game controllers)."
}

# Remove the staged OEM driver package(s) matching our INF.
$oem = pnputil /enum-drivers | Select-String -Context 0,4 'SimpleAudioSample.inf'
if ($oem) {
  Write-Host "Found staged package(s). To delete: pnputil /delete-driver oemNN.inf /uninstall /force"
  $oem
}

if ($DisableTestSigning) {
  bcdedit /set testsigning off | Out-Null
  Write-Warning "Disabled test signing. Reboot to apply."
}
Write-Host "Done."
