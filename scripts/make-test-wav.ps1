<#
.SYNOPSIS
  Generates a 5.1, 48 kHz, 16-bit test WAV with a distinct sine tone per channel,
  using the system ffmpeg. Channel order follows the 5.1 layout: FL FR FC LFE BL BR.

  Tones: FL=200 FR=300 FC=400 LFE=60 BL=600 BR=700 Hz — so you can confirm each
  channel ends up in the right speaker after AC3 decode.
#>
[CmdletBinding()]
param(
  [string]$Out = (Join-Path $PSScriptRoot '..\engine\test\test_5p1.wav'),
  [int]$Duration = 5
)

$ErrorActionPreference = 'Stop'
$Out = [System.IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null

# Build a single -filter_complex with one lavfi sine source per channel, then join to 5.1.
# (One arg, no per-input splatting — that mangles the filter string across shells.)
$freqs = 200,300,400,60,600,700
$parts  = @()
$labels = ''
for ($i = 0; $i -lt $freqs.Count; $i++) {
  $parts  += "sine=frequency=$($freqs[$i]):duration=$($Duration):sample_rate=48000[a$i]"
  $labels += "[a$i]"
}
$fc = ($parts -join ';') + ";${labels}join=inputs=$($freqs.Count):channel_layout=5.1[a]"

& ffmpeg -y -filter_complex $fc -map '[a]' -c:a pcm_s16le $Out
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed ($LASTEXITCODE)" }
Write-Host "Wrote $Out"
& ffprobe -hide_banner $Out
