// WavFile.h — minimal RIFF/WAVE reader for the offline encoder test harness.
//
// Supports PCM (int16/int32) and IEEE float32, including WAVE_FORMAT_EXTENSIBLE.
// Returns interleaved sample data exactly as stored.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WavFile
{
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  bool     isFloat = false;
  std::vector<uint8_t> data; // interleaved, native-endian

  size_t bytesPerFrame() const { return static_cast<size_t>(channels) * (bitsPerSample / 8); }
  size_t numFrames() const { return bytesPerFrame() ? data.size() / bytesPerFrame() : 0; }

  // Loads a WAV file. Returns true on success; on failure sets `error`.
  static bool Load(const std::string& path, WavFile& out, std::string& error);
};
