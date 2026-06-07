// main.cpp — virtual-ac3-encoder engine entry point.
//
// Wires capture -> ring buffer -> AC3/IEC61937 passthrough:
//   * captures multichannel PCM from a virtual cable's recording endpoint (shared)
//   * encodes it to AC3 and streams it to a chosen optical output (exclusive passthrough)
//
//   engine.exe --list
//   engine.exe --in "CABLE Output" --out "Digital Output"
//   engine.exe --in-id {0.0.1...} --out-id {0.0.0...} --bitrate 640000
//
#include "ComUtil.h"
#include "Config.h"
#include "DeviceEnum.h"
#include "RingBuffer.h"
#include "WasapiCapture.h"
#include "WasapiPassthrough.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static std::atomic_bool g_stop{false};

static BOOL WINAPI CtrlHandler(DWORD type)
{
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
  {
    g_stop.store(true);
    return TRUE;
  }
  return FALSE;
}

static Config ParseArgs(int argc, char** argv)
{
  Config c;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    auto next = [&]() -> std::wstring { return (i + 1 < argc) ? Widen(argv[++i]) : std::wstring(); };
    if (a == "--list")            c.listDevices = true;
    else if (a == "--probe")      c.probe = true;
    else if (a == "--loopback")   c.loopback = true;
    else if (a == "--mon")        c.monitor = true;
    else if (a == "--duration" && i + 1 < argc) c.durationSeconds = (int)std::strtol(argv[++i], nullptr, 10);
    else if (a == "--in")         c.inName = next();
    else if (a == "--in-id")      c.inId = next();
    else if (a == "--out")        c.outName = next();
    else if (a == "--out-id")     c.outId = next();
    else if (a == "--out-spdif")  c.outAutoSpdif = true;
    else if (a == "--bitrate" && i + 1 < argc) c.bitRate = std::strtoll(argv[++i], nullptr, 10);
    else if (a == "--safe" && i + 1 < argc)    c.safeFrames = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else std::fprintf(stderr, "ignoring unknown arg: %s\n", a.c_str());
  }
  return c;
}

static bool ResolveCapture(const Config& c, ComPtr<IMMDevice>& dev, EndpointInfo& info)
{
  // In loopback mode the "input" is a RENDER endpoint (the virtual sink); otherwise it's a
  // real capture endpoint.
  const EDataFlow flow = c.loopback ? eRender : eCapture;
  if (!c.inId.empty())
  {
    if (!DeviceEnum::GetById(c.inId, dev)) { std::fprintf(stderr, "input id not found\n"); return false; }
    info.id = c.inId;
    return true;
  }
  if (DeviceEnum::FindByNameSubstring(flow, c.inName, dev, info))
    return true;
  std::fprintf(stderr, "input device matching \"%s\" not found among %s endpoints (try --list)\n",
               Narrow(c.inName.c_str()).c_str(), c.loopback ? "render" : "capture");
  return false;
}

static bool ResolveOutput(const Config& c, ComPtr<IMMDevice>& dev, EndpointInfo& info)
{
  if (!c.outId.empty())
  {
    if (!DeviceEnum::GetById(c.outId, dev)) { std::fprintf(stderr, "output id not found\n"); return false; }
    info.id = c.outId;
    return true;
  }
  if (!c.outName.empty())
  {
    if (DeviceEnum::FindByNameSubstring(eRender, c.outName, dev, info)) return true;
    std::fprintf(stderr, "output device matching \"%s\" not found (try --list)\n", Narrow(c.outName.c_str()).c_str());
    return false;
  }
  if (c.outAutoSpdif)
  {
    for (const auto& e : DeviceEnum::List(eRender))
      if (e.isSpdif) { info = e; return DeviceEnum::GetById(e.id, dev); }
    std::fprintf(stderr, "no SPDIF output endpoint found (try --list / --out)\n");
    return false;
  }
  std::fprintf(stderr, "no output specified; use --out, --out-id or --out-spdif (or --list)\n");
  return false;
}

// Capture-only diagnostic: report how much audio is flowing from the input. Non-intrusive
// (shared-mode capture/loopback; does not touch the optical output).
static int RunMonitor(const Config& c, IMMDevice* inDev)
{
  WasapiCapture cap;
  if (!cap.Init(inDev, c.loopback)) return 1;
  const CaptureFormat& f = cap.Format();
  const size_t bpf = f.bytesPerFrame() ? f.bytesPerFrame() : 1;
  RingBuffer ring(static_cast<size_t>(32) * 1536 * bpf);
  cap.SetRing(&ring);
  if (!cap.Start()) return 1;

  std::printf("Monitoring input for %d s (play something / Ctrl+C to stop)...\n", c.monitorSeconds);
  unsigned long long totalFrames = 0;
  for (int i = 0; i < c.monitorSeconds * 10 && !g_stop.load(); ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const size_t avail = ring.BytesAvailable();
    ring.Discard(avail);
    totalFrames += avail / bpf;
  }
  cap.Stop();

  const double secs = c.monitorSeconds > 0 ? c.monitorSeconds : 1;
  std::printf("Captured ~%llu frames (~%.0f frames/s; endpoint rate %u Hz). %s\n", totalFrames,
              totalFrames / secs, f.sampleRate,
              totalFrames > 0 ? "OK: audio is flowing." : "No data (silent / nothing playing to it).");
  return 0;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered so logs aren't lost on abnormal exit

  ComApartment com;
  if (!com.ok()) { std::fprintf(stderr, "CoInitializeEx failed\n"); return 1; }

  Config cfg = ParseArgs(argc, argv);

  if (cfg.listDevices)
  {
    std::printf("Render (output) endpoints:\n");
    DeviceEnum::Print(DeviceEnum::List(eRender));
    std::printf("\nCapture (input) endpoints:\n");
    DeviceEnum::Print(DeviceEnum::List(eCapture));
    return 0;
  }

  if (cfg.probe)
  {
    std::printf("AC3 passthrough support (IsFormatSupported, exclusive):\n");
    for (const auto& e : DeviceEnum::List(eRender))
    {
      ComPtr<IMMDevice> d;
      if (!DeviceEnum::GetById(e.id, d))
        continue;
      bool ok48 = WasapiPassthrough::ProbeAc3(d.Get(), 48000);
      bool ok44 = WasapiPassthrough::ProbeAc3(d.Get(), 44100);
      std::printf("  %-48s %s  AC3@48k:%s  AC3@44.1k:%s\n", Narrow(e.name.c_str()).c_str(),
                  e.isSpdif ? "[SPDIF]" : "       ", ok48 ? "YES" : "no ", ok44 ? "YES" : "no ");
    }
    return 0;
  }

  ComPtr<IMMDevice> inDev;
  EndpointInfo inInfo;
  if (!ResolveCapture(cfg, inDev, inInfo)) return 1;
  std::printf("Input   : %s %s\n", Narrow(inInfo.name.c_str()).c_str(),
              cfg.loopback ? "(loopback)" : "(capture)");

  SetConsoleCtrlHandler(CtrlHandler, TRUE);

  if (cfg.monitor)
    return RunMonitor(cfg, inDev.Get());

  ComPtr<IMMDevice> outDev;
  EndpointInfo outInfo;
  if (!ResolveOutput(cfg, outDev, outInfo)) return 1;
  std::printf("Output  : %s %s\n", Narrow(outInfo.name.c_str()).c_str(), outInfo.isSpdif ? "[SPDIF]" : "");

  WasapiCapture capture;
  if (!capture.Init(inDev.Get(), cfg.loopback)) return 1;

  const CaptureFormat& cf = capture.Format();
  // Ring sized to ~8 AC3 packets of capture audio (drift absorber).
  const size_t ringBytes = static_cast<size_t>(8) * 1536 * cf.bytesPerFrame();
  RingBuffer ring(ringBytes);
  capture.SetRing(&ring);

  WasapiPassthrough::Params pp;
  pp.bitRate = cfg.bitRate;
  pp.safeFrames = cfg.safeFrames;

  WasapiPassthrough out;
  if (!out.Init(outDev.Get(), &ring, cf, pp)) return 1;

  if (!out.Start())     { std::fprintf(stderr, "passthrough start failed\n"); return 1; }
  if (!capture.Start()) { std::fprintf(stderr, "capture start failed\n"); out.Stop(); return 1; }

  if (cfg.durationSeconds > 0)
    std::printf("Running for %d s (or Ctrl+C)...\n", cfg.durationSeconds);
  else
    std::printf("Running. Press Ctrl+C to stop.\n");

  auto start = std::chrono::steady_clock::now();
  while (!g_stop.load())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (cfg.durationSeconds > 0 &&
        std::chrono::steady_clock::now() - start >= std::chrono::seconds(cfg.durationSeconds))
      break;
  }

  std::printf("\nStopping...\n");
  capture.Stop();
  out.Stop();
  return 0;
}
