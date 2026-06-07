// RingBuffer.h
//
// Single-producer / single-consumer lock-free byte ring buffer.
//
// This is the Windows stand-in for SoundPusher's TPCircularBuffer: it decouples the
// capture clock (producer) from the output/render clock (consumer) and absorbs the drift
// between them. Unlike TPCircularBuffer it does not double-map memory; the consumer reads
// into its own contiguous staging buffer (one memcpy per AC3 packet — negligible), which
// keeps it fully portable.
//
// Safe for exactly one producer thread and one consumer thread. Capacity is rounded up to
// a power of two so index wrapping is a mask.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

class RingBuffer
{
public:
  explicit RingBuffer(size_t capacityBytes)
  {
    size_t cap = 1;
    while (cap < capacityBytes)
      cap <<= 1;
    buf_.resize(cap);
    mask_ = cap - 1;
  }

  size_t Capacity() const { return buf_.size(); }

  // Consumer view: bytes ready to read.
  size_t BytesAvailable() const
  {
    const uint64_t head = head_.load(std::memory_order_acquire);
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    return static_cast<size_t>(head - tail);
  }

  // Producer view: free space.
  size_t SpaceAvailable() const
  {
    return Capacity() - BytesAvailableProducer();
  }

  // Producer: append up to n bytes. Returns bytes actually written (may be < n if full).
  size_t Write(const void* src, size_t n)
  {
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    const size_t free = Capacity() - static_cast<size_t>(head - tail);
    if (n > free)
      n = free;

    const size_t off = static_cast<size_t>(head) & mask_;
    const size_t first = std::min(n, Capacity() - off);
    std::memcpy(buf_.data() + off, src, first);
    if (n > first)
      std::memcpy(buf_.data(), static_cast<const uint8_t*>(src) + first, n - first);

    head_.store(head + n, std::memory_order_release);
    return n;
  }

  // Consumer: copy up to n bytes WITHOUT consuming. Returns bytes copied.
  size_t Peek(void* dst, size_t n) const
  {
    const uint64_t head = head_.load(std::memory_order_acquire);
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    const size_t avail = static_cast<size_t>(head - tail);
    if (n > avail)
      n = avail;

    const size_t off = static_cast<size_t>(tail) & mask_;
    const size_t first = std::min(n, Capacity() - off);
    std::memcpy(dst, buf_.data() + off, first);
    if (n > first)
      std::memcpy(static_cast<uint8_t*>(dst) + first, buf_.data(), n - first);
    return n;
  }

  // Consumer: advance the read pointer by n bytes (clamped to what's available).
  void Discard(size_t n)
  {
    const size_t avail = BytesAvailable();
    if (n > avail)
      n = avail;
    tail_.store(tail_.load(std::memory_order_relaxed) + n, std::memory_order_release);
  }

  // Consumer: Peek + Discard.
  size_t Read(void* dst, size_t n)
  {
    const size_t got = Peek(dst, n);
    Discard(got);
    return got;
  }

  // Consumer: drop everything (used on (re)start).
  void Clear()
  {
    tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
  }

private:
  // Producer's own view of fill level (uses relaxed head since producer owns it).
  size_t BytesAvailableProducer() const
  {
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    return static_cast<size_t>(head - tail);
  }

  std::vector<uint8_t> buf_;
  size_t mask_ = 0;
  std::atomic<uint64_t> head_{0}; // total bytes written (producer)
  std::atomic<uint64_t> tail_{0}; // total bytes read (consumer)
};
