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

$freqs = 200,300,400,60,600,700
$inputs = @()
foreach ($fr in $freqs) {
  $inputs += @('-f','lavfi','-i', "sine=frequency=$fr:duration=$Duration:sample_rate=48000")
}
$fc = '[0:a][1:a][2:a][3:a][4:a][5:a]join=inputs=6:channel_layout=5.1[a]'

& ffmpeg -y @inputs -filter_complex $fc -map '[a]' -c:a pcm_s16le $Out
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed ($LASTEXITCODE)" }
Write-Host "Wrote $Out"
& ffprobe -hide_banner $Out
