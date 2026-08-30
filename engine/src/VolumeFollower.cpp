// VolumeFollower.cpp — see VolumeFollower.h.
#include "VolumeFollower.h"

#include "Gain.h"

#include <endpointvolume.h> // see the note in VolumeFollower.h: kept out of the header

#include <cmath>

VolumeFollower::VolumeFollower() = default;

VolumeFollower::~VolumeFollower()
{
  Stop();
  if (stopEvent_) CloseHandle(stopEvent_);
}

bool VolumeFollower::Init(IMMDevice* dev)
{
  if (!dev) return false;

  HRESULT hr = dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &vol_);
  if (FAILED(hr))
  {
    std::fprintf(stderr, "[VolumeFollower] endpoint has no volume control (%s); "
                         "running at unity gain\n", HrStr(hr).c_str());
    return false;
  }

  const float g = ReadGain();
  gain_.store(g, std::memory_order_relaxed);

  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual-reset
  if (!stopEvent_) return false;
  running_.store(true);
  thread_ = std::thread(&VolumeFollower::ThreadProc, this);

  std::printf("[VolumeFollower] following the input device's volume/mute (currently %.0f%%)\n",
              g * 100.0f);
  return true;
}

void VolumeFollower::Stop()
{
  if (!running_.exchange(false))
    return;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
}

// Master volume as a linear amplitude. GetMasterVolumeLevel returns the dB attenuation the
// endpoint's volume node is set to, so 10^(dB/20) reproduces Windows' own taper exactly;
// the scalar is only a fallback for devices that don't report a dB range.
float VolumeFollower::ReadGain()
{
  if (!vol_)
    return 1.0f;

  BOOL muted = FALSE;
  if (SUCCEEDED(vol_->GetMute(&muted)) && muted)
    return 0.0f;

  float db = 0.0f;
  if (SUCCEEDED(vol_->GetMasterVolumeLevel(&db)))
    return ClampGain(std::pow(10.0f, db / 20.0f));

  float scalar = 1.0f;
  if (SUCCEEDED(vol_->GetMasterVolumeLevelScalar(&scalar)))
    return ClampGain(scalar);

  return 1.0f;
}

void VolumeFollower::ThreadProc()
{
  // This thread makes its own COM calls, so it needs an apartment of its own (MTA, matching
  // the process apartment, so the calls stay direct).
  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  while (running_.load(std::memory_order_relaxed))
  {
    if (WaitForSingleObject(stopEvent_, kPollMs) == WAIT_OBJECT_0)
      break;
    gain_.store(ReadGain(), std::memory_order_relaxed);
  }

  if (SUCCEEDED(comHr)) CoUninitialize();
}
