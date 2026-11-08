// Single-producer / single-consumer lock-free ring buffer (bulk copy in, bulk copy out).
//
// Provenance: ported from the archive's engine/buffer/spsc_ring.hpp (namespace renamed).
//
// THREADING CONTRACT
//   * Exactly one thread calls the producer functions: push(), writable_items().
//   * Exactly one thread calls the consumer functions: pop(), discard(), readable_items().
//   * capacity_items() may be called from any thread (it never changes).
//   * A side may move to another thread over time, but never two threads on one side at once.
//     After a hand-over (thread join, mutex, an atomic acquire/release pair) the new thread
//     is the side.
//   * Construct and destroy the ring while no other thread is using it.
//
// REAL-TIME SAFETY
//   Storage is allocated once in the constructor. push(), pop(), discard() and the *_items()
//   queries never allocate, lock, or block, and finish in a bounded number of steps whatever
//   the other thread is doing: they are wait-free (not merely lock-free). A full ring makes
//   push() take fewer items (possibly 0); an empty ring makes pop() return 0. The caller
//   decides whether to drop, retry later, or yield; neither side ever waits for the other.
//   T should be a plain trivially copyable sample type (float, std::complex<float>, ...):
//   items are copied with operator= in a loop.
//
// CAPACITY
//   Capacity is the constructor argument rounded up to a power of two (minimum 2); read it
//   back with capacity_items(). Every slot is usable (full means capacity_items() items, not
//   capacity - 1). Counts are items of T, not bytes. A push larger than the free space is a
//   partial push: it returns how many items it took and the caller keeps the rest. The ring
//   never overwrites unread data.
//
// MEMORY ORDERING (why acquire/release is enough)
//   head_ counts items ever pushed; only the producer writes it. tail_ counts items ever
//   popped; only the consumer writes it. Both are monotonic counters; size_t wrap-around is
//   harmless because capacity is a power of two and we only use differences and masking.
//
//   Producer: copies items into the slots, THEN stores head_ with release. Consumer: loads
//   head_ with acquire, THEN reads those slots. The release store publishes every slot write
//   before it to any thread whose acquire load sees that head_ (or a later one), so the
//   consumer never reads a half-written item. Symmetrically the consumer reads its slots,
//   THEN stores tail_ with release, and the producer loads tail_ with acquire before reusing
//   slots, so the producer never overwrites a slot the consumer is still reading.
//
//   A side reads its OWN index relaxed: no other thread writes it, so it always sees its
//   latest value. Sequential consistency is not needed: there are two threads and two
//   single-writer variables, and each cross-thread dependency is one "data written, then index
//   published" pair. A stale value of the other side's index is always safe; it only makes the
//   ring look fuller (producer's view) or emptier (consumer's view) than it really is.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace bl {

template <typename T>
class SpscRing {
 public:
  // Capacity is rounded up to the next power of two (minimum 2).
  explicit SpscRing(std::size_t min_capacity_items)
      : capacity_items_(round_up_pow2(min_capacity_items)),
        mask_(capacity_items_ - 1),
        storage_(capacity_items_) {}

  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  std::size_t capacity_items() const { return capacity_items_; }

  // Producer thread only. Copies as many items as fit; returns the number copied (0 if full).
  std::size_t push(std::span<const T> items) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t free_items = capacity_items_ - (head - tail);
    const std::size_t count = std::min(items.size(), free_items);
    for (std::size_t i = 0; i < count; ++i) {
      storage_[(head + i) & mask_] = items[i];
    }
    head_.store(head + count, std::memory_order_release);
    return count;
  }

  // Producer thread only. Free slots (at least this many; the consumer may free more).
  std::size_t writable_items() const {
    return capacity_items_ - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire));
  }

  // Consumer thread only. Items ready to pop (at least this many; the producer may add more).
  std::size_t readable_items() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
  }

  // Consumer thread only. Copies up to out.size() items; returns the number copied (0 if empty).
  std::size_t pop(std::span<T> out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t count = std::min(out.size(), head - tail);
    for (std::size_t i = 0; i < count; ++i) {
      out[i] = storage_[(tail + i) & mask_];
    }
    tail_.store(tail + count, std::memory_order_release);
    return count;
  }

  // Consumer thread only. Drops everything readable; returns the number dropped. Consumer
  // only because it moves tail_, which only the consumer may write.
  std::size_t discard() {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    tail_.store(head, std::memory_order_release);
    return head - tail;
  }

 private:
  static std::size_t round_up_pow2(std::size_t n) {
    std::size_t p = 2;
    while (p < n) p <<= 1;
    return p;
  }

  const std::size_t capacity_items_;
  const std::size_t mask_;
  std::vector<T> storage_;
  // Separate cache lines so producer and consumer do not false-share.
  // 128 bytes covers Apple Silicon (128-byte lines) as well as x86/most ARM (64-byte lines).
  // Possible optimization if profiling shows push()/pop() are hot: each side keeps a
  // non-atomic cached copy of the other side's index and only re-loads the atomic when
  // the cached value says the ring is full (producer) or empty (consumer). That avoids
  // pulling the other core's cache line on most calls.
  alignas(128) std::atomic<std::size_t> head_{0};
  alignas(128) std::atomic<std::size_t> tail_{0};
};

}  // namespace bl
