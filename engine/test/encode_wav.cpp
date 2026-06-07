// encode_wav.cpp — offline harness for the AC3/SPDIF encoder (Phase 1 verification).
//
//   encode_wav <input.wav> <output.spdif> [bitrate_bps]
//
// Reads a multichannel WAV, encodes it to AC3 wrapped in IEC 61937 bursts, and writes the
// raw S/PDIF byte stream. Verify the result with the system ffmpeg, e.g.:
//
//   ffmpeg -f spdif -i output.spdif -f null -
//   ffprobe -f spdif output.spdif
//
// which demuxes the IEC 61937 framing and decodes the AC3 inside it.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WavFile.h"
#include "../src/SpdifEncoder.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    std::fprintf(stderr, "usage: %s <input.wav> <output.spdif> [bitrate_bps]\n", argv[0]);
    return 2;
  }
  const std::string inPath = argv[1];
  const std::string outPath = argv[2];
  const int64_t bitRate = (argc >= 4) ? std::strtoll(argv[3], nullptr, 10) : 640000;

  WavFile wav;
  std::string err;
  if (!WavFile::Load(inPath, wav, err))
  {
    std::fprintf(stderr, "WAV load failed: %s\n", err.c_str());
    return 1;
  }
  std::printf("Input: %u Hz, %u ch, %u-bit %s, %zu frames\n", wav.sampleRate, wav.channels,
              wav.bitsPerSample, wav.isFloat ? "float" : "int", wav.numFrames());

  AVSampleFormat inFmt;
  if (wav.bitsPerSample == 16)      inFmt = AV_SAMPLE_FMT_S16;
  else if (wav.isFloat)             inFmt = AV_SAMPLE_FMT_FLT;
  else                              inFmt = AV_SAMPLE_FMT_S32;

  SpdifEncoder::Params p;
  p.sampleRate = static_cast<int>(wav.sampleRate);
  p.bitRate = bitRate;
  p.inSampleFmt = inFmt;
  av_channel_layout_default(&p.inLayout, wav.channels);

  SpdifEncoder enc;
  bool ok = enc.Init(p);
  av_channel_layout_uninit(&p.inLayout);
  if (!ok)
  {
    std::fprintf(stderr, "encoder init failed (sample rate must be 48000/44100/32000 for AC3)\n");
    return 1;
  }

  const int fpp = enc.FramesPerPacket();
  const size_t bytesPerFrame = wav.bytesPerFrame();
  const size_t chunkBytes = static_cast<size_t>(fpp) * bytesPerFrame;
  std::printf("Encoder: AC3 %lld bps, %d frames/packet, %d in-channels\n",
              static_cast<long long>(bitRate), fpp, enc.InChannels());

  std::FILE* out = std::fopen(outPath.c_str(), "wb");
  if (!out) { std::fprintf(stderr, "cannot open output %s\n", outPath.c_str()); return 1; }

  std::vector<uint8_t> staging(chunkBytes);
  std::vector<uint8_t> burst(SpdifEncoder::kMaxBytesPerPacket);

  size_t offset = 0;          // byte offset into wav.data
  long long bursts = 0, totalBytes = 0;
  while (offset < wav.data.size())
  {
    size_t avail = wav.data.size() - offset;
    size_t take = avail < chunkBytes ? avail : chunkBytes;
    std::memcpy(staging.data(), wav.data.data() + offset, take);
    if (take < chunkBytes)
      std::memset(staging.data() + take, 0, chunkBytes - take); // zero-pad final packet
    offset += take;

    int n = enc.EncodePacket(staging.data(), burst.data(), static_cast<int>(burst.size()));
    if (n < 0) { std::fprintf(stderr, "encode error at frame %zu\n", offset / bytesPerFrame); break; }
    if (n > 0)
    {
      std::fwrite(burst.data(), 1, static_cast<size_t>(n), out);
      ++bursts;
      totalBytes += n;
    }
  }
  std::fclose(out);

  std::printf("Wrote %lld IEC 61937 bursts (%lld bytes) to %s\n", bursts, totalBytes, outPath.c_str());
  return 0;
}
