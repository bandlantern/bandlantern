// Real-time check: hot-path calls must not allocate. This executable replaces the global
// operator new with a counting one, which is why it is separate from the other test binaries.
// Provenance: counting-allocator scaffold ported from the archive's engine/tests/rt_alloc_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "api/channel.hpp"
#include "api/server.hpp"
#include "buffer/spsc_ring.hpp"
#include "pipeline/pipeline.hpp"

namespace {
// A global is the only way to observe operator new; it is confined to this test binary.
std::atomic<std::size_t> g_allocations{0};
}  // namespace

// The nothrow forms are replaced too: the standard says they default to the throwing ones,
// but ASan's runtime defines its own, which would pair its new with our free(). Aligned forms
// are not counted; nothing under test uses over-aligned heap objects.
void* operator new(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
// GCC cannot see that the replaced new above uses malloc(), so it flags free() here.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

// Allocations made while running fn.
template <typename Fn>
std::size_t allocations_during(Fn&& fn) {
  const std::size_t before = g_allocations.load(std::memory_order_relaxed);
  fn();
  return g_allocations.load(std::memory_order_relaxed) - before;
}

std::vector<float> make_dbfs(std::size_t bins) {
  std::vector<float> v(bins);
  for (std::size_t i = 0; i < bins; ++i) v[i] = -60.0f - 0.01f * static_cast<float>(i);
  return v;
}

}  // namespace

TEST_CASE("the allocation counter sees allocations", "[rt]") {
  CHECK(allocations_during([] { std::vector<int> v(16); }) >= 1);
}

TEST_CASE("SpscRing push, pop, readable, writable, and discard do not allocate", "[rt][ring]") {
  bl::SpscRing<float> ring(1024);
  const std::vector<float> in(300, 0.5f);
  std::vector<float> out(200);

  std::size_t moved = 0;
  const std::size_t count = allocations_during([&] {
    for (int block = 0; block < 200; ++block) {  // runs full, empty, and wrapped
      moved += ring.push(in);
      moved += ring.pop(out);
      moved += ring.writable_items() + ring.readable_items();
      if (block % 50 == 49) moved += ring.discard();
    }
  });
  CHECK(count == 0);
  CHECK(moved > 0);
}

TEST_CASE("Channel::publish does not allocate, active, full, or inactive", "[rt][channel]") {
  bl::api::Channel channel;  // 8 frame buffers are allocated here, outside the region
  const std::vector<float> dbfs = make_dbfs(bl::api::kMaxBins);
  bl::SpectrumRow row;
  row.rate_sps = 2.4e6;
  row.center_freq_hz = 100e6;
  row.dbfs = dbfs;

  const std::size_t inactive = allocations_during([&] { channel.publish(row); });
  CHECK(inactive == 0);

  channel.activate();
  const std::size_t count = allocations_during([&] {
    for (int i = 0; i < 20; ++i) {  // 8 fit, the rest take the drop path
      row.sample_index = static_cast<std::uint64_t>(i) * 2048;
      channel.publish(row);
    }
    channel.deactivate();
    channel.publish(row);
  });
  CHECK(count == 0);
  CHECK(channel.dropped() == 12);
  // Drain outside the region so the check is that the rows really were queued.
  int queued = 0;
  while (channel.front() != nullptr) {
    channel.pop();
    ++queued;
  }
  CHECK(queued == 8);
}

TEST_CASE("Server::on_row does not allocate into preallocated channels", "[rt][server]") {
  bl::api::Server server{bl::api::ServerConfig{}};  // constructed, never started
  const std::vector<float> dbfs = make_dbfs(2048);
  bl::SpectrumRow row;
  row.rate_sps = 2.4e6;
  row.center_freq_hz = 100e6;
  row.dbfs = dbfs;
  bl::SpectrumRow empty = row;
  empty.dbfs = {};

  const std::size_t count = allocations_during([&] {
    for (int i = 0; i < 50; ++i) server.on_row(row);
    server.on_row(empty);  // ignored without allocating
  });
  CHECK(count == 0);
}
