<#
.SYNOPSIS
  Downloads a Windows x64 *shared* FFmpeg build (DLLs + import libs + headers) into
  third_party/ffmpeg so the engine can link libavcodec/libavformat/libavutil/libswresample.

  Uses BtbN's prebuilt FFmpeg-Builds (https://github.com/BtbN/FFmpeg-Builds). LGPL shared
  variant is sufficient: the native AC3 encoder and the spdif muxer are both included.

  Result layout:
    third_party/ffmpeg/bin      *.dll  (shipped next to the engine .exe)
    third_party/ffmpeg/include  libav*/  headers
    third_party/ffmpeg/lib      *.lib  (MSVC import libraries)
#>
[CmdletBinding()]
param(
  # Which release branch to prefer in the BtbN "latest" rolling release.
  [string]$Prefer = 'n7.1',
  [string]$Variant = 'win64-lgpl-shared',
  [string]$DestDir = (Join-Path $PSScriptRoot '..\third_party\ffmpeg')
)

$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$DestDir = [System.IO.Path]::GetFullPath($DestDir)
$tmp = Join-Path $env:TEMP ("ffmpeg-fetch-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host "Querying BtbN FFmpeg-Builds 'latest' release for a $Variant asset (prefer $Prefer)..."
$rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/latest' `
  -Headers @{ 'User-Agent' = 'virtual-ac3-encoder' }

$assets = $rel.assets | Where-Object { $_.name -like "*$Variant.zip" }
if (-not $assets) { throw "No assets matching *$Variant.zip found in BtbN latest release." }

$asset = $assets | Where-Object { $_.name -like "*$Prefer-*" } | Select-Object -First 1
if (-not $asset) { $asset = $assets | Select-Object -First 1 }   # fall back to whatever shared build exists
Write-Host "Selected: $($asset.name)  ($([math]::Round($asset.size/1MB,1)) MB)"

$zip = Join-Path $tmp $asset.name
Write-Host "Downloading..."
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -Headers @{ 'User-Agent' = 'virtual-ac3-encoder' }

Write-Host "Extracting..."
Expand-Archive -Path $zip -DestinationPath $tmp -Force
$inner = Get-ChildItem $tmp -Directory | Where-Object { $_.Name -like 'ffmpeg-*' } | Select-Object -First 1
if (-not $inner) { throw "Could not find extracted ffmpeg-* folder." }

# Flatten: copy bin/include/lib up into DestDir
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
foreach ($sub in 'bin','include','lib') {
  $src = Join-Path $inner.FullName $sub
  if (Test-Path $src) {
    $dst = Join-Path $DestDir $sub
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    Copy-Item $src $dst -Recurse -Force
  }
}

# Write a small marker recording what we fetched.
"$($asset.name)`n$(Get-Date -Format o)" | Set-Content (Join-Path $DestDir 'FETCHED.txt')

Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Done. FFmpeg dev files are in: $DestDir"
Get-ChildItem $DestDir | Select-Object Name | Format-Table -AutoSize
