// test_ringbuffer.cpp — unit tests for the SPSC lock-free RingBuffer.
#include "doctest.h"
#include "RingBuffer.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

TEST_CASE("capacity rounds up to a power of two")
{
  CHECK(RingBuffer(1000).Capacity() == 1024);
  CHECK(RingBuffer(1024).Capacity() == 1024);
  CHECK(RingBuffer(1025).Capacity() == 2048);
  CHECK(RingBuffer(1).Capacity() == 1);
}

TEST_CASE("write/read round-trip preserves data")
{
  RingBuffer rb(64);
  uint8_t in[10];
  for (int i = 0; i < 10; ++i) in[i] = static_cast<uint8_t>(i);
  CHECK(rb.Write(in, 10) == 10);
  CHECK(rb.BytesAvailable() == 10);

  uint8_t out[10] = {0};
  CHECK(rb.Read(out, 10) == 10);
  CHECK(rb.BytesAvailable() == 0);
  for (int i = 0; i < 10; ++i) CHECK(out[i] == i);
}

TEST_CASE("peek does not consume; discard advances")
{
  RingBuffer rb(64);
  uint8_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  rb.Write(in, 8);

  uint8_t out[8] = {0};
  CHECK(rb.Peek(out, 8) == 8);
  CHECK(rb.BytesAvailable() == 8); // peek must not consume
  CHECK(out[0] == 1);
  CHECK(out[7] == 8);

  rb.Discard(3);
  CHECK(rb.BytesAvailable() == 5);
  uint8_t out2[5] = {0};
  rb.Peek(out2, 5);
  CHECK(out2[0] == 4); // first 3 were discarded
}

TEST_CASE("data wraps correctly across the buffer boundary")
{
  RingBuffer rb(16); // capacity 16
  std::vector<uint8_t> a(12);
  for (int i = 0; i < 12; ++i) a[i] = static_cast<uint8_t>(i + 1);
  CHECK(rb.Write(a.data(), 12) == 12);
  std::vector<uint8_t> tmp(12);
  rb.Read(tmp.data(), 12); // tail now at 12

  std::vector<uint8_t> b(8);
  for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>(100 + i);
  CHECK(rb.Write(b.data(), 8) == 8); // writes 4 then wraps for 4 more

  std::vector<uint8_t> out(8);
  CHECK(rb.Read(out.data(), 8) == 8);
  for (int i = 0; i < 8; ++i) CHECK(out[i] == 100 + i);
}

TEST_CASE("write clamps at capacity (no overrun)")
{
  RingBuffer rb(16); // capacity 16
  std::vector<uint8_t> a(20, 0xAB);
  CHECK(rb.Write(a.data(), 20) == 16);
  CHECK(rb.BytesAvailable() == 16);
  CHECK(rb.SpaceAvailable() == 0);
}

TEST_CASE("read/discard clamp at available")
{
  RingBuffer rb(64);
  uint8_t in[5] = {1, 2, 3, 4, 5};
  rb.Write(in, 5);
  uint8_t out[10] = {0};
  CHECK(rb.Read(out, 10) == 5);
  rb.Discard(100); // should not underflow
  CHECK(rb.BytesAvailable() == 0);
}

TEST_CASE("clear empties the buffer")
{
  RingBuffer rb(64);
  uint8_t in[5] = {1, 2, 3, 4, 5};
  rb.Write(in, 5);
  rb.Clear();
  CHECK(rb.BytesAvailable() == 0);
  CHECK(rb.SpaceAvailable() == rb.Capacity());
}

TEST_CASE("SPSC concurrent transfer preserves every byte in order")
{
  // Producer sends a known sequence (wrapping mod 256); consumer reads it back.
  // Sums match iff no byte is lost, duplicated, or reordered.
  constexpr size_t N = 1u << 18; // 256 KiB
  RingBuffer rb(4096);
  uint64_t writeSum = 0, readSum = 0;
  bool seqOk = true;

  std::thread prod([&] {
    uint8_t v = 0;
    size_t sent = 0;
    size_t guard = 0;
    while (sent < N && guard++ < N * 1000)
    {
      uint8_t b = v;
      if (rb.Write(&b, 1) == 1) { writeSum += b; ++v; ++sent; }
      else std::this_thread::yield();
    }
  });

  std::thread cons([&] {
    uint8_t expected = 0;
    size_t got = 0;
    size_t guard = 0;
    while (got < N && guard++ < N * 1000)
    {
      uint8_t b;
      if (rb.Read(&b, 1) == 1)
      {
        readSum += b;
        if (b != expected) seqOk = false;
        ++expected;
        ++got;
      }
      else std::this_thread::yield();
    }
  });

  prod.join();
  cons.join();
  CHECK(seqOk);
  CHECK(writeSum == readSum);
}
