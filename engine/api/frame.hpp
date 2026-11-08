// Binary spectrum frame encoder. Layout: docs/api/frames.md (v1), ADR-008.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "pipeline/pipeline.hpp"

namespace bl::api {

inline constexpr std::size_t kFrameHeaderBytes = 48;
inline constexpr std::size_t kMaxBins = 16384;  // largest messages.md `set_fft` size

constexpr std::size_t frame_size_bytes(std::size_t bin_count) {
  return kFrameHeaderBytes + bin_count * 4;
}

// Writes header + payload into out and returns the bytes written, or 0 if the row has more
// than kMaxBins bins or out is too small. No allocation (called on the pipeline's thread).
std::size_t encode_spectrum_frame(const SpectrumRow& row, std::uint64_t seq,
                                  std::span<std::uint8_t> out) noexcept;

}  // namespace bl::api
