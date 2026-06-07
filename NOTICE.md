# Third-party notices & attributions

This project (MIT, see `LICENSE`) builds on the following. None of their binaries are
committed to this repository; they are fetched or installed separately.

## Kernel driver — `driver/`
Derived from Microsoft's **Windows-driver-samples** "SimpleAudioSample"
(<https://github.com/microsoft/Windows-driver-samples>), which is licensed under the **MIT
License**, Copyright (c) Microsoft Corporation. The original per-file copyright headers are
retained. Modifications here: trimmed to a single 5.1 render-only endpoint, rebranded
"Virtual AC3 Encoder (5.1)", and the INF target-OS decoration lowered for Windows 10.

## Engine design — `engine/`
The real-time encode/forward design (output clock as master, lock-free ring buffer, periodic
drift-trim) is modeled on **SoundPusher** by Daniel Vollmer
(<https://codeberg.org/q-p/SoundPusher>), **MIT License**. No SoundPusher source is copied; it
is reimplemented for Windows/WASAPI against the modern FFmpeg API.

## FFmpeg (external dependency)
The engine dynamically links **FFmpeg** libraries (`libavcodec`, `libavformat`, `libavutil`,
`libswresample`) for AC3 encoding and IEC 61937 muxing. FFmpeg is licensed under the
**LGPL-2.1-or-later** (the configuration used here). FFmpeg is **not** included in this repo;
`scripts/fetch-ffmpeg.ps1` downloads a prebuilt shared (LGPL) build. See
<https://ffmpeg.org> and <https://www.ffmpeg.org/legal.html>.

## VB-CABLE (optional runtime, not included)
On systems with Secure Boot enabled (where the bundled test-signed driver cannot load),
**VB-CABLE** by VB-Audio Software (<https://vb-audio.com/Cable/>) can be used as the virtual
audio source instead. VB-CABLE is proprietary freeware and is **not** distributed here;
download it from VB-Audio.

## Reference only (not distributed)
- `jakemoroni/audio_async_loopback` — referenced for Windows S/PDIF AC3 loopback timing.
- Kodi / xbmc AudioEngine (GPL-2.0-or-later) — read for WASAPI passthrough patterns; **no code copied**.
