// Provenance: ported from the archive's engine/tests/buffer_ring_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "buffer/spsc_ring.hpp"

using bl::SpscRing;

TEST_CASE("ring push/pop basics and capacity rounding", "[buffer]") {
  SpscRing<int> ring(5);
  CHECK(ring.capacity_items() == 8);
  const std::vector<int> in{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  CHECK(ring.push(in) == 8);  // only 8 fit
  CHECK(ring.push(in) == 0);
  std::vector<int> out(3);
  CHECK(ring.pop(out) == 3);
  CHECK(out == std::vector<int>{1, 2, 3});
  CHECK(ring.push(std::span<const int>(in).first(2)) == 2);  // wraps around
  std::vector<int> rest(16);
  CHECK(ring.pop(rest) == 7);
  CHECK(rest[4] == 8);
  CHECK(rest[5] == 1);
  CHECK(rest[6] == 2);
}

TEST_CASE("ring capacity minimum and empty pop", "[buffer]") {
  SpscRing<int> ring(0);
  CHECK(ring.capacity_items() == 2);
  std::vector<int> out(4);
  CHECK(ring.pop(out) == 0);
  CHECK(ring.readable_items() == 0);
  CHECK(ring.writable_items() == 2);
}

TEST_CASE("ring discard drops what is readable", "[buffer]") {
  SpscRing<int> ring(8);
  const std::vector<int> in{1, 2, 3, 4, 5};
  ring.push(in);
  CHECK(ring.discard() == 5);
  CHECK(ring.readable_items() == 0);
  CHECK(ring.push(in) == 5);  // the whole capacity is free again
  CHECK(ring.push(in) == 3);
}

TEST_CASE("ring SPSC stress keeps order across two threads", "[buffer]") {
  constexpr std::uint64_t total = 2'000'000;
  SpscRing<std::uint64_t> ring(1024);

  std::thread producer([&] {
    std::vector<std::uint64_t> batch(37);
    std::uint64_t next = 0;
    while (next < total) {
      const std::size_t n = std::min<std::uint64_t>(batch.size(), total - next);
      for (std::size_t i = 0; i < n; ++i) batch[i] = next + i;
      std::span<const std::uint64_t> pending(batch.data(), n);
      while (!pending.empty()) {
        const std::size_t pushed = ring.push(pending);
        pending = pending.subspan(pushed);
        if (pushed == 0) std::this_thread::yield();
      }
      next += n;
    }
  });

  std::uint64_t expected = 0;
  bool in_order = true;
  std::vector<std::uint64_t> buf(101);
  while (expected < total) {
    const std::size_t got = ring.pop(buf);
    if (got == 0) {
      std::this_thread::yield();
      continue;
    }
    for (std::size_t i = 0; i < got; ++i) in_order = in_order && (buf[i] == expected + i);
    expected += got;
  }
  producer.join();
  CHECK(in_order);  // in order with the right count means nothing was lost or duplicated
  CHECK(expected == total);
  CHECK(ring.readable_items() == 0);
}
