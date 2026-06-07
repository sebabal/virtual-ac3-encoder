// WavFile.cpp — see WavFile.h.

#include "WavFile.h"

#include <cstdio>
#include <cstring>

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// First 2 bytes of a WAVEFORMATEXTENSIBLE SubFormat GUID encode the actual format tag.
constexpr uint16_t WAVE_FORMAT_PCM = 0x0001;
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

} // namespace

bool WavFile::Load(const std::string& path, WavFile& out, std::string& error)
{
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { error = "cannot open " + path; return false; }

  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 44) { error = "file too small to be WAV"; std::fclose(f); return false; }

  std::vector<uint8_t> buf(static_cast<size_t>(size));
  if (std::fread(buf.data(), 1, buf.size(), f) != buf.size())
  { error = "short read"; std::fclose(f); return false; }
  std::fclose(f);

  if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
  { error = "not a RIFF/WAVE file"; return false; }

  bool haveFmt = false, haveData = false;
  uint16_t fmtTag = 0;
  size_t pos = 12;
  while (pos + 8 <= buf.size())
  {
    const char* id = reinterpret_cast<const char*>(buf.data() + pos);
    uint32_t chunkSize = rd32(buf.data() + pos + 4);
    size_t body = pos + 8;
    if (body + chunkSize > buf.size())
      chunkSize = static_cast<uint32_t>(buf.size() - body); // tolerate truncation

    if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16)
    {
      const uint8_t* p = buf.data() + body;
      fmtTag = rd16(p + 0);
      out.channels = rd16(p + 2);
      out.sampleRate = rd32(p + 4);
      out.bitsPerSample = rd16(p + 14);
      if (fmtTag == WAVE_FORMAT_EXTENSIBLE && chunkSize >= 40)
        fmtTag = rd16(p + 24); // SubFormat GUID's leading format tag
      out.isFloat = (fmtTag == WAVE_FORMAT_IEEE_FLOAT);
      if (fmtTag != WAVE_FORMAT_PCM && fmtTag != WAVE_FORMAT_IEEE_FLOAT)
      { error = "unsupported WAV format tag"; return false; }
      haveFmt = true;
    }
    else if (std::memcmp(id, "data", 4) == 0)
    {
      out.data.assign(buf.data() + body, buf.data() + body + chunkSize);
      haveData = true;
    }

    pos = body + chunkSize + (chunkSize & 1); // chunks are word-aligned
  }

  if (!haveFmt) { error = "no fmt chunk"; return false; }
  if (!haveData) { error = "no data chunk"; return false; }
  if (out.bitsPerSample != 16 && out.bitsPerSample != 32)
  { error = "only 16-bit PCM or 32-bit (PCM/float) supported"; return false; }
  return true;
}
