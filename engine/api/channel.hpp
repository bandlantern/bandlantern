// Per-socket spectrum queue: pipeline DSP thread (producer) -> server I/O thread (consumer).
//
// Why SPSC rather than a shared queue with a lock: the DSP thread must never block, allocate,
// or take a lock. Each socket owns one Channel with exactly one producer (whoever calls
// Server::on_row) and one consumer (the I/O thread), so a wait-free ring with acquire/release
// head/tail indices is enough. All storage (8 frame buffers of the largest size) is allocated
// up front.
//
// The producer encodes each frame straight into its slot with the socket's seq, so the consumer
// writes the slot bytes to the socket with no further copy.
//
// Reuse across sockets: the consumer bumps `epoch` on activation. The producer sees the new
// epoch, restarts seq at 0, and stamps slots with it; the consumer skips slots stamped with an
// older epoch. So the consumer never writes producer-owned state.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "api/frame.hpp"

namespace bl::api {

class Channel {
 public:
  static constexpr std::uint64_t kCapacity = 8;  // frames.md "Back-pressure"

  struct Slot {
    std::vector<std::uint8_t> bytes;  // sized once, never reallocated
    std::size_t len = 0;
    std::uint32_t epoch = 0;
  };

  Channel() {
    for (auto& s : slots_) s.bytes.resize(frame_size_bytes(kMaxBins));
  }

  // ---- producer (DSP thread): wait-free, no allocation ----
  void publish(const SpectrumRow& row) noexcept {
    if (!active_.load(std::memory_order_acquire)) return;
    const std::uint32_t e = epoch_.load(std::memory_order_acquire);
    if (e != producer_epoch_) {
      producer_epoch_ = e;
      next_seq_ = 0;
    }
    const std::uint64_t seq = next_seq_++;  // increments even if the row is dropped
    const std::uint64_t h = head_.load(std::memory_order_relaxed);
    const std::uint64_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= kCapacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    Slot& s = slots_[h % kCapacity];
    s.len = encode_spectrum_frame(row, seq, s.bytes);
    s.epoch = e;
    head_.store(h + 1, std::memory_order_release);
  }

  // ---- consumer (I/O thread) ----
  // Oldest queued frame of the current epoch, or nullptr. Stays valid until pop().
  const Slot* front() noexcept {
    const std::uint32_t e = epoch_.load(std::memory_order_relaxed);
    std::uint64_t t = tail_.load(std::memory_order_relaxed);
    const std::uint64_t h = head_.load(std::memory_order_acquire);
    while (t < h && (slots_[t % kCapacity].epoch != e || slots_[t % kCapacity].len == 0)) ++t;
    tail_.store(t, std::memory_order_release);
    return t < h ? &slots_[t % kCapacity] : nullptr;
  }

  void pop() noexcept { tail_.fetch_add(1, std::memory_order_release); }

  // Total rows dropped because the queue was full (monotonic, never reset).
  std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

  void activate() noexcept {
    epoch_.fetch_add(1, std::memory_order_release);
    tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    active_.store(true, std::memory_order_release);
  }

  void deactivate() noexcept { active_.store(false, std::memory_order_release); }

  // I/O thread only: slot ownership bookkeeping.
  bool in_use = false;

 private:
  std::array<Slot, kCapacity> slots_;
  std::atomic<std::uint64_t> head_{0};
  std::atomic<std::uint64_t> tail_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint32_t> epoch_{0};
  std::atomic<bool> active_{false};
  // Producer-only state.
  std::uint32_t producer_epoch_ = 0;
  std::uint64_t next_seq_ = 0;
};

}  // namespace bl::api
