// Gain.h
//
// In-place gain (volume/mute) for a block of interleaved PCM, with a linear ramp across the
// block so a volume or mute change lands as a 32 ms fade instead of a step (a step at a
// packet boundary is audible as a click).
//
// Why the engine applies volume itself: it reads the virtual cable through WASAPI
// *loopback*, which taps the mix before the endpoint's volume/mute node, and it writes an
// IEC 61937 bitstream in exclusive mode, which bypasses every Windows volume control (and
// couldn't be scaled anyway — it's compressed). So the Windows slider only does anything if
// we apply it here, to the PCM, before it reaches the AC3 encoder. See VolumeFollower.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Sample formats WasapiCapture can hand us (interleaved).
enum class PcmSampleFormat
{
  Unknown,
  F32, // 32-bit float   (the usual WASAPI shared-mode mix format)
  S16, // 16-bit signed
  S32, // 32-bit signed
};

inline float ClampGain(float g)
{
  if (!(g > 0.0f)) return 0.0f; // also catches NaN
  return g > 1.0f ? 1.0f : g;   // never amplify: the AC3 encoder gets no headroom back
}

// Scales `frames` interleaved frames of `channels` samples in place, ramping the gain
// linearly from g0 (first frame) to g1 (last frame). Every sample in a frame gets the same
// gain. A no-op for unity gain, an unknown format, or an empty block.
inline void ApplyGainRamp(void* data, size_t frames, unsigned channels, PcmSampleFormat fmt,
                          float g0, float g1)
{
  if (!data || frames == 0 || channels == 0 || fmt == PcmSampleFormat::Unknown)
    return;
  g0 = ClampGain(g0);
  g1 = ClampGain(g1);
  if (g0 == 1.0f && g1 == 1.0f)
    return;

  const double step = frames > 1 ? (static_cast<double>(g1) - g0) / static_cast<double>(frames - 1) : 0.0;
  double g = g0;

  switch (fmt)
  {
  case PcmSampleFormat::F32:
  {
    float* p = static_cast<float*>(data);
    for (size_t f = 0; f < frames; ++f, g += step)
      for (unsigned c = 0; c < channels; ++c, ++p)
        *p = static_cast<float>(*p * g);
    break;
  }
  case PcmSampleFormat::S16:
  {
    int16_t* p = static_cast<int16_t*>(data);
    for (size_t f = 0; f < frames; ++f, g += step)
      for (unsigned c = 0; c < channels; ++c, ++p)
        *p = static_cast<int16_t>(std::lrint(*p * g)); // |g| <= 1, so this cannot overflow
    break;
  }
  case PcmSampleFormat::S32:
  {
    int32_t* p = static_cast<int32_t*>(data);
    for (size_t f = 0; f < frames; ++f, g += step)
      for (unsigned c = 0; c < channels; ++c, ++p)
        *p = static_cast<int32_t>(std::llrint(*p * g));
    break;
  }
  case PcmSampleFormat::Unknown:
  default:
    break;
  }
}
