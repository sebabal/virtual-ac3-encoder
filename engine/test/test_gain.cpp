// test_gain.cpp — unit tests for the volume/mute gain stage (Gain.h).
#include "doctest.h"
#include "Gain.h"

#include <cstdint>
#include <vector>

TEST_CASE("unity gain leaves float samples untouched")
{
  std::vector<float> s = {-1.0f, -0.25f, 0.0f, 0.5f, 1.0f, 0.75f};
  const std::vector<float> orig = s;
  ApplyGainRamp(s.data(), 3, 2, PcmSampleFormat::F32, 1.0f, 1.0f);
  for (size_t i = 0; i < s.size(); ++i)
    CHECK(s[i] == doctest::Approx(orig[i]));
}

TEST_CASE("constant gain scales every sample of every frame equally")
{
  std::vector<float> s = {1.0f, -1.0f, 0.5f, -0.5f, 0.25f, -0.25f};
  ApplyGainRamp(s.data(), 3, 2, PcmSampleFormat::F32, 0.5f, 0.5f);
  CHECK(s[0] == doctest::Approx(0.5f));
  CHECK(s[1] == doctest::Approx(-0.5f));
  CHECK(s[2] == doctest::Approx(0.25f));
  CHECK(s[3] == doctest::Approx(-0.25f));
  CHECK(s[4] == doctest::Approx(0.125f));
  CHECK(s[5] == doctest::Approx(-0.125f));
}

TEST_CASE("mute zeroes the block")
{
  std::vector<float> s(12, 0.8f);
  ApplyGainRamp(s.data(), 2, 6, PcmSampleFormat::F32, 0.0f, 0.0f);
  for (float v : s)
    CHECK(v == doctest::Approx(0.0f));
}

TEST_CASE("gain ramps linearly across the block, per frame")
{
  // 5 frames, 1 channel, 0.0 -> 1.0: expect 0, .25, .5, .75, 1.
  std::vector<float> s(5, 1.0f);
  ApplyGainRamp(s.data(), 5, 1, PcmSampleFormat::F32, 0.0f, 1.0f);
  CHECK(s[0] == doctest::Approx(0.0f));
  CHECK(s[1] == doctest::Approx(0.25f));
  CHECK(s[2] == doctest::Approx(0.5f));
  CHECK(s[3] == doctest::Approx(0.75f));
  CHECK(s[4] == doctest::Approx(1.0f));

  // Both samples of a frame share that frame's gain (no intra-frame slope).
  std::vector<float> st(6, 1.0f);
  ApplyGainRamp(st.data(), 3, 2, PcmSampleFormat::F32, 0.0f, 1.0f);
  CHECK(st[0] == doctest::Approx(st[1]));
  CHECK(st[2] == doctest::Approx(st[3]));
  CHECK(st[4] == doctest::Approx(st[5]));
  CHECK(st[0] == doctest::Approx(0.0f));
  CHECK(st[2] == doctest::Approx(0.5f));
  CHECK(st[4] == doctest::Approx(1.0f));
}

TEST_CASE("gain is clamped to [0,1] and never amplifies")
{
  std::vector<float> s = {0.5f, 0.5f};
  ApplyGainRamp(s.data(), 2, 1, PcmSampleFormat::F32, 4.0f, -2.0f); // clamps to 1.0 -> 0.0
  CHECK(s[0] == doctest::Approx(0.5f));
  CHECK(s[1] == doctest::Approx(0.0f));
}

TEST_CASE("integer formats scale without overflowing")
{
  std::vector<int16_t> s16 = {32767, -32768, 1000, -1000};
  ApplyGainRamp(s16.data(), 4, 1, PcmSampleFormat::S16, 1.0f, 1.0f); // unity: unchanged
  CHECK(s16[0] == 32767);
  CHECK(s16[1] == -32768);

  ApplyGainRamp(s16.data(), 4, 1, PcmSampleFormat::S16, 0.5f, 0.5f);
  CHECK(s16[0] == 16384); // 32767 * 0.5 = 16383.5, rounds to even/nearest
  CHECK(s16[1] == -16384);
  CHECK(s16[2] == 500);
  CHECK(s16[3] == -500);

  std::vector<int32_t> s32 = {2147483647, -2147483647 - 1, 4000};
  ApplyGainRamp(s32.data(), 3, 1, PcmSampleFormat::S32, 0.25f, 0.25f);
  CHECK(s32[0] == 536870912);
  CHECK(s32[1] == -536870912);
  CHECK(s32[2] == 1000);
}

TEST_CASE("unknown sample format is a no-op rather than corrupting the block")
{
  std::vector<float> s = {0.5f, 0.5f};
  ApplyGainRamp(s.data(), 2, 1, PcmSampleFormat::Unknown, 0.0f, 0.0f);
  CHECK(s[0] == doctest::Approx(0.5f));
  CHECK(s[1] == doctest::Approx(0.5f));
}
