// WasapiCapture.cpp — see WasapiCapture.h.
#include "WasapiCapture.h"

#include <mmreg.h>
#include <ksmedia.h>

#include <avrt.h>
#include <vector>

WasapiCapture::~WasapiCapture()
{
  Stop();
  if (dataEvent_) CloseHandle(dataEvent_);
  if (stopEvent_) CloseHandle(stopEvent_);
}

bool WasapiCapture::Init(IMMDevice* dev, bool loopback)
{
  loopback_ = loopback;
  HR_FAIL(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_), "Activate IAudioClient");

  WAVEFORMATEX* mix = nullptr;
  HR_FAIL(client_->GetMixFormat(&mix), "GetMixFormat");

  fmt_.channels = mix->nChannels;
  fmt_.sampleRate = mix->nSamplesPerSec;
  fmt_.bits = mix->wBitsPerSample;
  fmt_.isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
  if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
  {
    auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
    fmt_.channelMask = ext->dwChannelMask;
    fmt_.isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  }

  // Shared mode. Loopback streams cannot be event-driven, so they are polled instead.
  // periodicity must be 0 in shared mode.
  const DWORD flags = loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
  // Ask for a generous endpoint buffer. A loopback stream can't be event-driven, so it is
  // polled -- and if a poll lands later than the buffer is long, WASAPI has already discarded
  // the overrun and that audio is gone for good. Every such gap is a permanent input deficit
  // the ring can never make up, which shows up downstream as starvation and dropouts. 20 ms
  // was far too tight on a box with several other audio stacks running.
  REFERENCE_TIME bufferHns = 2000000; // 200 ms; WASAPI may enlarge as needed
  HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferHns, 0, mix, nullptr);
  CoTaskMemFree(mix);
  if (FAILED(hr)) { std::fprintf(stderr, "[WasapiCapture] Initialize failed: %s\n", HrStr(hr).c_str()); return false; }

  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual-reset
  if (!stopEvent_) return false;
  if (!loopback_)
  {
    dataEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dataEvent_) return false;
    HR_FAIL(client_->SetEventHandle(dataEvent_), "SetEventHandle");
  }
  HR_FAIL(client_->GetService(__uuidof(IAudioCaptureClient), &capture_), "GetService(IAudioCaptureClient)");

  UINT32 endpointFrames = 0;
  if (SUCCEEDED(client_->GetBufferSize(&endpointFrames)) && fmt_.sampleRate)
    std::printf("[WasapiCapture] endpoint buffer = %u frames (~%.0f ms)\n", endpointFrames,
                1000.0 * endpointFrames / fmt_.sampleRate);

  std::printf("[WasapiCapture] %s: %u ch, %u Hz, %u-bit %s, mask=0x%X\n",
              loopback_ ? "loopback" : "capture", fmt_.channels, fmt_.sampleRate, fmt_.bits,
              fmt_.isFloat ? "float" : "int", fmt_.channelMask);
  return true;
}

bool WasapiCapture::Start()
{
  if (!ring_)
  {
    std::fprintf(stderr, "[WasapiCapture] SetRing() not called before Start()\n");
    return false;
  }
  if (running_.exchange(true))
    return true;
  ResetEvent(stopEvent_);
  HR_FAIL(client_->Start(), "client Start");
  thread_ = std::thread(&WasapiCapture::ThreadProc, this);
  return true;
}

void WasapiCapture::Stop()
{
  if (!running_.exchange(false))
    return;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
  if (client_) client_->Stop();
}

void WasapiCapture::ThreadProc()
{
  DWORD taskIndex = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

  const size_t bpf = fmt_.bytesPerFrame();

  auto drain = [&]() {
    UINT32 packet = 0;
    while (SUCCEEDED(capture_->GetNextPacketSize(&packet)) && packet > 0)
    {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
        break;

      const size_t bytes = static_cast<size_t>(frames) * bpf;
      if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
        discontinuities_.fetch_add(1, std::memory_order_relaxed); // WASAPI dropped input

      if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
      {
        // Push silence so the timeline stays continuous.
        static thread_local std::vector<uint8_t> zeros;
        if (zeros.size() < bytes) zeros.assign(bytes, 0);
        ring_->Write(zeros.data(), bytes);
      }
      else if (data)
      {
        const size_t wrote = ring_->Write(data, bytes);
        if (wrote < bytes) // ring full: the consumer isn't keeping up
          droppedFrames_.fetch_add((bytes - wrote) / bpf, std::memory_order_relaxed);
      }
      capture_->ReleaseBuffer(frames);
    }
  };

  // Report input loss at most once every few seconds, and only when it changes.
  uint64_t lastDisc = 0, lastDropped = 0;
  ULONGLONG lastReport = GetTickCount64();
  auto report = [&]() {
    if (GetTickCount64() - lastReport < 5000)
      return;
    lastReport = GetTickCount64();
    const uint64_t d = Discontinuities(), f = DroppedFrames();
    if (d == lastDisc && f == lastDropped)
      return;
    std::fprintf(stderr, "[WasapiCapture] input loss: %llu gap(s) from WASAPI, %llu frame(s) "
                         "dropped into a full ring\n",
                 static_cast<unsigned long long>(d - lastDisc),
                 static_cast<unsigned long long>(f - lastDropped));
    lastDisc = d;
    lastDropped = f;
  };

  if (loopback_)
  {
    // Loopback streams aren't event-driven: poll, waking early if asked to stop.
    while (running_.load(std::memory_order_relaxed))
    {
      if (WaitForSingleObject(stopEvent_, 5) == WAIT_OBJECT_0) // ~5 ms poll
        break;
      drain();
      report();
    }
  }
  else
  {
    HANDLE waits[2] = { dataEvent_, stopEvent_ };
    while (running_.load(std::memory_order_relaxed))
    {
      DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
      if (w == WAIT_OBJECT_0 + 1) // stop
        break;
      drain();
      report();
    }
  }

  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}
