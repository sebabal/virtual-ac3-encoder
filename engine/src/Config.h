// Config.h — engine runtime configuration (CLI-driven for now; file-based in Phase 4).
#pragma once

#include <string>

struct Config
{
  bool listDevices = false;
  bool probe = false;    // probe all render endpoints for AC3 passthrough support, then exit
  bool loopback = false; // capture the input as a RENDER endpoint via WASAPI loopback
                         // (the render-only virtual-driver architecture)
  bool monitor = false;  // capture-only diagnostic: report input throughput, then exit
  int  monitorSeconds = 5;
  int  durationSeconds = 0; // 0 = run until Ctrl+C; otherwise auto-stop after N seconds

  // Capture (input) endpoint — the virtual cable's recording side.
  std::wstring inName = L"CABLE Output"; // friendly-name substring (VB-CABLE default)
  std::wstring inId;                     // exact endpoint id (overrides inName)

  // Output endpoint — the optical / Toslink device.
  std::wstring outName;  // friendly-name substring
  std::wstring outId;    // exact endpoint id (overrides outName / auto)
  bool outAutoSpdif = false; // pick the first render endpoint with SPDIF form factor

  int64_t  bitRate = 640000;
  uint32_t safeFrames = 1536;

  // Follow the input endpoint's Windows volume/mute and apply it to the PCM before
  // encoding: "follow" (default) or "off". Needed because a loopback capture is tapped
  // before the endpoint volume node and an exclusive-mode bitstream bypasses it entirely,
  // so without this the Windows slider for the virtual cable does nothing at all.
  std::string volume = "follow";

  // Stereo->5.1 upmix mode for <=2ch input: "off" (swr default) or "surround"
  // (FFmpeg `surround` FFT upmix). Multichannel input is always downmixed to 5.1.
  // Default "surround": this tool targets a 5.1 optical path, so stereo sources use all speakers.
  std::string upmix = "surround";
};
