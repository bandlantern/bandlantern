// WAV IQ playback source (our own reader, written from the Microsoft/IBM "Multimedia
// Programming Interface and Data Specifications 1.0" (1991), RIFF and WAVE sections, and
// Microsoft's WAVE_FORMAT_EXTENSIBLE documentation). Contract: docs/api/testdata.md "WAV IQ".
// Nothing here throws; failures are returned as IqOpenError "bad_format" or "not_found".
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "io/iq_source.hpp"

namespace bl {

struct WavLayout {
  IqMeta meta;                          // center_freq_hz stays 0: a WAV header has no centre frequency
  std::uint32_t block_align_bytes = 0;  // one I,Q frame
  std::uint64_t data_offset_bytes = 0;
  std::uint64_t data_size_bytes = 0;  // clamped to the file, whole frames only
};

// Walks the RIFF chunks of a seekable stream (any order, odd sizes padded). Requires a
// 2-channel fmt chunk (PCM 8- or 16-bit, or IEEE float 32-bit; plain or EXTENSIBLE) and a
// data chunk.
std::variant<WavLayout, IqOpenError> parse_wav_header(std::istream& in);

// Centre frequency from the first "<digits>Hz" token between '_' separators in the file
// name (e.g. "baseband_162475000Hz_13-04-51.wav"). nullopt if there is none.
std::optional<double> center_freq_from_filename(std::string_view filename);

// testdata.md: cs16 -> cf32 is s / 32768 for I then Q (little-endian int16).
// Converts min(out.size(), iq_bytes.size() / 4) samples from interleaved bytes.
void convert_cs16_le_to_cf32(std::span<const std::uint8_t> iq_bytes, std::span<Cf32> out);

class WavReader final : public IIqSource {
 public:
  // Opens the file on the calling thread; call read() only from the reader thread.
  // meta().center_freq_hz comes from the file name, or 0.
  static std::variant<std::unique_ptr<WavReader>, IqOpenError> open(const std::filesystem::path& path, bool loop);

  const IqMeta& meta() const override { return layout_.meta; }
  std::size_t read(std::span<Cf32> out) override;
  bool failed() const override { return failed_; }

 private:
  WavReader() = default;
  std::size_t read_some(std::span<Cf32> out);
  void rewind();

  WavLayout layout_;
  std::ifstream file_;
  std::uint64_t remaining_bytes_ = 0;  // of the data chunk
  bool loop_ = false;
  bool failed_ = false;
  std::vector<std::uint8_t> raw_bytes_;  // scratch, sized at open()
};

}  // namespace bl
