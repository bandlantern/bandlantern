// SigMF playback source (our own reader, written from the SigMF core v1.2 spec).
// Contract: docs/api/testdata.md. Nothing here throws; failures are returned as IqOpenError
// with code "bad_format" or "not_found" (docs/api/messages.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "io/iq_source.hpp"

namespace bl {

// Parses .sigmf-meta JSON text. Requires global.core:datatype (cu8 | cf32_le),
// global.core:sample_rate (> 0) and captures[0].core:frequency. Later captures are ignored:
// meta().center_freq_hz is the first capture's.
std::variant<IqMeta, IqOpenError> parse_sigmf_meta(std::string_view meta_json_text);

// testdata.md: cu8 -> cf32 is (byte - 127.5) / 127.5 for I then Q.
// Converts min(out.size(), iq_bytes.size() / 2) samples from interleaved bytes.
void convert_cu8_to_cf32(std::span<const std::uint8_t> iq_bytes, std::span<Cf32> out);

class SigmfReader final : public IIqSource {
 public:
  // meta_path: "x.sigmf-meta"; the data file is "x.sigmf-data" beside it.
  // Opens files on the calling thread; call read() only from the reader thread.
  static std::variant<std::unique_ptr<SigmfReader>, IqOpenError> open(const std::filesystem::path& meta_path,
                                                                      bool loop);

  const IqMeta& meta() const override { return meta_; }
  std::size_t read(std::span<Cf32> out) override;
  bool failed() const override { return failed_; }

 private:
  SigmfReader() = default;
  std::size_t read_some(std::span<Cf32> out);

  IqMeta meta_;
  std::ifstream data_;
  bool loop_ = false;
  bool failed_ = false;
  std::vector<std::uint8_t> raw_bytes_;  // scratch, sized at open()
};

}  // namespace bl
