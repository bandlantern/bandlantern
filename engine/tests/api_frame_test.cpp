// The C++ encoder matches the golden frame byte for byte (docs/api/frames.md "Golden vector").
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "api/frame.hpp"

namespace {

std::vector<std::uint8_t> read_golden_frame() {
  std::ifstream file(BL_TESTDATA_DIR "/api/spectrum_frame_v1.bin", std::ios::binary);
  REQUIRE(file);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The golden values (frames.md); f32 literals, as the vector stores them.
constexpr std::array<float, 8> kGoldenDbfs = {-200.0f, -120.5f, -90.25f, -60.0f,
                                              0.0f,    -3.0103f, -45.5f, -110.75f};

bl::SpectrumRow golden_row() {
  bl::SpectrumRow row;
  row.sample_index = 160'000;
  row.center_freq_hz = 100'100'000.0;
  row.rate_sps = 2'400'000.0;
  row.dbfs = kGoldenDbfs;
  return row;
}

}  // namespace

TEST_CASE("encoder matches the golden frame byte for byte", "[api][frame]") {
  const std::vector<std::uint8_t> golden = read_golden_frame();
  std::vector<std::uint8_t> out(bl::api::frame_size_bytes(kGoldenDbfs.size()));

  const std::size_t written = bl::api::encode_spectrum_frame(golden_row(), 7, out);

  REQUIRE(written == golden.size());
  CHECK(out == golden);
}

TEST_CASE("encoder refuses a buffer that is too small", "[api][frame]") {
  std::vector<std::uint8_t> out(bl::api::frame_size_bytes(kGoldenDbfs.size()) - 1);

  CHECK(bl::api::encode_spectrum_frame(golden_row(), 7, out) == 0);
}

TEST_CASE("encoder refuses more than kMaxBins bins", "[api][frame]") {
  std::vector<float> dbfs(bl::api::kMaxBins + 1, -100.0f);
  bl::SpectrumRow row = golden_row();
  row.dbfs = dbfs;
  std::vector<std::uint8_t> out(bl::api::frame_size_bytes(dbfs.size()));

  CHECK(bl::api::encode_spectrum_frame(row, 0, out) == 0);
}
