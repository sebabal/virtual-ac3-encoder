<#
.SYNOPSIS
  Installs the Virtual AC3 Encoder virtual audio driver (test-signed).

  MUST be run elevated (Administrator). For a test-signed driver to load:
    1. Secure Boot must be OFF (UEFI setting - this script only warns).
    2. Test signing must be ON (bcdedit) - pass -EnableTestSigning, then REBOOT.
    3. The test certificate must be trusted (this script imports it).

.EXAMPLE
  # First time (enables test signing; reboot afterwards, then re-run to install the device):
  powershell -ExecutionPolicy Bypass -File scripts\install-driver.ps1 -EnableTestSigning
#>
[CmdletBinding()]
param(
  [string]$PackageDir = (Join-Path $PSScriptRoot '..\driver\x64\Release\package'),
  [switch]$EnableTestSigning
)
$ErrorActionPreference = 'Stop'

# --- elevation ---
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)) {
  throw "Run this script elevated (Administrator)."
}

$PackageDir = [System.IO.Path]::GetFullPath($PackageDir)
$inf = Join-Path $PackageDir 'SimpleAudioSample.inf'
$sys = Join-Path $PackageDir 'SimpleAudioSample.sys'
if (-not (Test-Path $inf)) { throw "INF not found: $inf  (build the driver first)" }

# --- Secure Boot warning ---
try {
  if (Confirm-SecureBootUEFI) {
    Write-Warning "Secure Boot is ON - test-signed drivers will NOT load. Disable it in UEFI, or attestation-sign the driver."
  } else { Write-Host "Secure Boot: off (ok for test signing)." }
} catch { Write-Host "Secure Boot state: unknown ($($_.Exception.Message))." }

# --- test signing ---
$tsOn = bcdedit /enum '{current}' | Select-String 'testsigning\s+Yes'
if ($tsOn) {
  Write-Host "Test signing: already ON."
} elseif ($EnableTestSigning) {
  bcdedit /set testsigning on | Out-Null
  Write-Warning "Enabled test signing. REBOOT now, then re-run this script (without -EnableTestSigning) to install the device."
} else {
  Write-Warning "Test signing is OFF. Re-run with -EnableTestSigning and reboot first."
}

# --- trust the test certificate (self-signed WDKTestCert) ---
$sig = Get-AuthenticodeSignature $sys
if ($sig.SignerCertificate) {
  foreach ($store in 'Root','TrustedPublisher') {
    $st = New-Object Security.Cryptography.X509Certificates.X509Store($store,'LocalMachine')
    $st.Open('ReadWrite'); $st.Add($sig.SignerCertificate); $st.Close()
    Write-Host "Imported test cert into LocalMachine\$store ($($sig.SignerCertificate.Subject))."
  }
} else {
  Write-Warning "Could not read the signing certificate from $sys."
}

# --- install the root-enumerated device ---
$devcon = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Tools' -Recurse -Filter devcon.exe -EA SilentlyContinue |
          Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $devcon) { throw "devcon.exe not found in the WDK Tools folder." }

Write-Host "Installing device ROOT\SimpleAudioSample ..."
& $devcon install $inf "ROOT\SimpleAudioSample"
if ($LASTEXITCODE -ne 0) { Write-Warning "devcon returned $LASTEXITCODE (a reboot may be pending if test signing was just enabled)." }

Write-Host ""
Write-Host "Done. After a reboot (if test signing was just turned on), look for 'Virtual AC3 Encoder (5.1)'"
Write-Host "in Windows Sound settings, then run the engine with:"
Write-Host "    engine.exe --loopback --in `"Virtual AC3 Encoder`" --out `"Realtek Digital Output`""
