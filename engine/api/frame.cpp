#include "api/frame.hpp"

#include <bit>
#include <limits>

namespace bl::api {
namespace {

static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "frame format assumes IEEE-754 float/double");

// Explicit byte-by-byte little-endian stores: no dependence on host endianness or struct
// layout (frames.md "Header").
void put_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v);
  p[1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::uint8_t* p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

void put_u64(std::uint8_t* p, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

}  // namespace

std::size_t encode_spectrum_frame(const SpectrumRow& row, std::uint64_t seq,
                                  std::span<std::uint8_t> out) noexcept {
  const std::size_t bin_count = row.dbfs.size();
  if (bin_count > kMaxBins) return 0;
  const std::size_t total = frame_size_bytes(bin_count);
  if (out.size() < total) return 0;

  std::uint8_t* p = out.data();
  p[0] = 'B';
  p[1] = 'L';
  p[2] = 'F';
  p[3] = 0;
  put_u16(p + 4, 1);  // version
  put_u16(p + 6, 1);  // kind: spectrum row
  put_u16(p + 8, static_cast<std::uint16_t>(kFrameHeaderBytes));
  put_u16(p + 10, 1);  // payload_format: f32 dBFS
  put_u32(p + 12, static_cast<std::uint32_t>(bin_count));
  put_u64(p + 16, seq);
  put_u64(p + 24, row.sample_index);
  put_u64(p + 32, std::bit_cast<std::uint64_t>(row.center_freq_hz));
  put_u64(p + 40, std::bit_cast<std::uint64_t>(row.rate_sps));
  for (std::size_t i = 0; i < bin_count; ++i) {
    put_u32(p + kFrameHeaderBytes + 4 * i, std::bit_cast<std::uint32_t>(row.dbfs[i]));
  }
  return total;
}

}  // namespace bl::api
