// VolumeFollower.h
//
// Tracks a WASAPI endpoint's master volume + mute (the Windows volume slider / OSD / media
// keys for that device) and publishes it as a linear amplitude gain the encode path can
// apply to the PCM it captures.
//
// Why this is needed at all: neither end of the pipeline honours the slider on its own.
//   * The input is read with AUDCLNT_STREAMFLAGS_LOOPBACK, and a loopback stream is tapped
//     after the per-app session volumes but *before* the endpoint's master volume/mute node.
//     Moving the virtual cable's slider (or muting it) therefore does not change one sample
//     of what we capture.
//   * The output is an IEC 61937 AC3 bitstream on an EXCLUSIVE-mode client: exclusive mode
//     bypasses the Windows audio engine (and its volume) entirely, and a compressed
//     bitstream can't be scaled without decoding it anyway.
// So the slider has to be applied by us, to the PCM, before it is encoded. See Gain.h.
//
// Polled (every kPollMs) on a dedicated thread rather than driven by
// IAudioEndpointVolumeCallback: the value is read with GetMasterVolumeLevel (dB), which is
// exactly the attenuation Windows itself would have applied — including the volume taper —
// and polling keeps COM calls off both the callback and the realtime render threads.
#pragma once

#include "ComUtil.h"

#include <mmdeviceapi.h>

// Deliberately NOT <endpointvolume.h>: it drags in ksmedia.h ahead of mmreg.h, and every
// translation unit that then includes this header loses WAVEFORMATEXTENSIBLE_IEC61937 (the
// AC3 passthrough format). Only VolumeFollower.cpp needs the real interface; the members
// below just need the name.
struct IAudioEndpointVolume;

#include <atomic>
#include <thread>

class VolumeFollower
{
public:
  // Out of line, both of them: with IAudioEndpointVolume incomplete here, an inline
  // constructor would instantiate ComPtr's destructor (for unwinding) and fail.
  VolumeFollower();
  ~VolumeFollower();
  VolumeFollower(const VolumeFollower&) = delete;
  VolumeFollower& operator=(const VolumeFollower&) = delete;

  // Starts following `dev`'s master volume. Returns false if the endpoint exposes no volume
  // control (Gain() then stays at 1.0, i.e. the engine behaves as before).
  bool Init(IMMDevice* dev);
  void Stop();

  // Linear amplitude in [0,1]: 0 when muted. Safe to call from the render thread.
  float Gain() const { return gain_.load(std::memory_order_relaxed); }

private:
  static constexpr DWORD kPollMs = 50; // well under the ~32 ms/packet ramp; imperceptible

  float ReadGain();
  void  ThreadProc();

  ComPtr<IAudioEndpointVolume> vol_;
  std::atomic<float>           gain_{1.0f};
  HANDLE                       stopEvent_ = nullptr;
  std::thread                  thread_;
  std::atomic_bool             running_{false};
};
