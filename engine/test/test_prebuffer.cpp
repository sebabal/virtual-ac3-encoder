// test_prebuffer.cpp — the refill gate that keeps a starved ring from stuttering.
#include "doctest.h"

#include "Prebuffer.h"

// One "packet" is what the render thread consumes per slot; the prime level is the backlog
// it insists on before (re)starting playback. Bytes here are arbitrary but consistent.
static constexpr size_t kPacket = 1000;
static constexpr size_t kPrime  = 4000;

TEST_CASE("Prebuffer starts refilling: a packet's worth is not enough to begin")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPrime);
  CHECK(gate.Refilling());

  CHECK_FALSE(gate.Take(0));
  CHECK_FALSE(gate.Take(kPacket));       // enough for one slot, but the cushion isn't built
  CHECK_FALSE(gate.Take(kPrime - 1));
  CHECK(gate.Refilling());

  CHECK(gate.Take(kPrime));              // cushion reached -> play
  CHECK_FALSE(gate.Refilling());
  CHECK(gate.Underruns() == 0);          // priming at startup is not an underrun
}

TEST_CASE("Prebuffer keeps playing while at least one packet is available")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPrime);
  REQUIRE(gate.Take(kPrime));

  CHECK(gate.Take(kPrime));
  CHECK(gate.Take(kPacket * 2));
  CHECK(gate.Take(kPacket));             // exactly one packet still plays
  CHECK_FALSE(gate.Refilling());
  CHECK(gate.Underruns() == 0);
}

TEST_CASE("Prebuffer re-arms after a starvation and waits for the whole cushion again")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPrime);
  REQUIRE(gate.Take(kPrime));

  CHECK_FALSE(gate.Take(kPacket - 1));   // starved
  CHECK(gate.Refilling());
  CHECK(gate.Underruns() == 1);

  // This is the bug this class exists to prevent: a trickling producer that keeps crossing
  // the one-packet line would otherwise alternate audio/silence packets (audible crackle).
  for (int i = 0; i < 10; ++i)
    CHECK_FALSE(gate.Take(kPacket + 1));
  CHECK(gate.Underruns() == 1);          // still the same starvation, not ten more

  CHECK(gate.Take(kPrime));              // refilled -> playing again
  CHECK_FALSE(gate.Refilling());
}

TEST_CASE("Prebuffer counts each distinct starvation")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPrime);
  for (int i = 1; i <= 3; ++i)
  {
    REQUIRE(gate.Take(kPrime));
    REQUIRE_FALSE(gate.Take(0));
    CHECK(gate.Underruns() == static_cast<uint64_t>(i));
  }
}

TEST_CASE("Prebuffer clamps a prime level below one packet, and Reset clears state")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPacket / 2);      // nonsensical cushion: never less than one packet
  CHECK_FALSE(gate.Take(kPacket - 1));
  CHECK(gate.Take(kPacket));

  REQUIRE_FALSE(gate.Take(0));
  REQUIRE(gate.Underruns() == 1);
  gate.Reset(kPacket, kPrime);
  CHECK(gate.Refilling());
  CHECK(gate.Underruns() == 0);
}

TEST_CASE("Prebuffer Rearm re-primes without losing the counter")
{
  Prebuffer gate;
  gate.Reset(kPacket, kPrime);
  REQUIRE(gate.Take(kPrime));
  REQUIRE_FALSE(gate.Take(0));

  gate.Rearm();                          // e.g. the stream was restarted
  CHECK(gate.Refilling());
  CHECK_FALSE(gate.Take(kPacket));
  CHECK(gate.Underruns() == 1);          // Rearm is not a starvation
}

TEST_CASE("Prebuffer with no packet size never plays")
{
  Prebuffer gate;                        // default-constructed, Reset never called
  CHECK_FALSE(gate.Take(1 << 20));
}
