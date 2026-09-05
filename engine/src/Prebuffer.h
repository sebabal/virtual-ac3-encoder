// Prebuffer.h
//
// The consumer-side refill gate: decides whether the render thread may take a packet from
// the ring, or must emit silence while the ring refills.
//
// Why this exists: the output clock is the master, so the render thread wants exactly one
// AC3 packet (1536 frames, ~32 ms) every cycle, while the capture side only trickles in
// whatever the loopback tap has. While nothing is playing on the virtual cable, WASAPI
// loopback delivers nothing at all, so the ring drains to empty. Without a gate, playback
// resumes the instant a single packet is available — and then the fill level hovers around
// that one-packet line, so the output alternates between real audio and AC3 silence. That
// is audible as crackle at the start of every sound.
//
// So: after any starvation, refuse to play until the ring holds the full target cushion
// (one cycle's worth plus the configured safety margin) again. Costs up to that cushion in
// added latency after a silent gap; buys clean audio.
#pragma once

#include <cstddef>
#include <cstdint>

class Prebuffer
{
public:
  Prebuffer() = default;

  // packetBytes : what the consumer takes per slot.
  // primeBytes  : backlog required before (re)starting playback; raised to packetBytes if
  //               smaller (a cushion below one packet would defeat the purpose).
  // Starts refilling and clears the starvation counter.
  void Reset(size_t packetBytes, size_t primeBytes)
  {
    packet_ = packetBytes;
    prime_ = primeBytes < packetBytes ? packetBytes : primeBytes;
    refilling_ = true;
    underruns_ = 0;
  }

  // Re-prime (e.g. the stream was restarted) without touching the counter.
  void Rearm() { refilling_ = true; }

  // One decision per packet slot, given the ring's current fill level. True = consume a
  // packet; false = emit silence this slot.
  bool Take(size_t available)
  {
    if (packet_ == 0)
      return false;
    if (refilling_)
    {
      if (available < prime_)
        return false;
      refilling_ = false; // cushion rebuilt; prime_ >= packet_, so this slot can be served
      return true;
    }
    if (available >= packet_)
      return true;
    refilling_ = true; // starved: stop playing until the whole cushion is back
    ++underruns_;
    return false;
  }

  bool     Refilling() const { return refilling_; }
  uint64_t Underruns() const { return underruns_; } // distinct starvations, not silent slots

private:
  size_t   packet_ = 0;
  size_t   prime_ = 0;
  bool     refilling_ = true;
  uint64_t underruns_ = 0;
};
