// What every IQ file source (SigMF, WAV) hands the playback pipeline: cf32 samples plus the
// file's rate and centre frequency. Error codes match docs/api/messages.md. Nothing here throws.
#pragma once

#include <complex>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace bl {

using Cf32 = std::complex<float>;  // sample format cf32: I = real, Q = imag, full scale ±1.0

// On-disk sample format; names match messages.md `source.datatype`.
enum class IqDatatype { cu8, cs16_le, cf32_le };

inline std::string_view datatype_name(IqDatatype datatype) {
  switch (datatype) {
    case IqDatatype::cu8: return "cu8";
    case IqDatatype::cs16_le: return "cs16_le";
    case IqDatatype::cf32_le: return "cf32_le";
  }
  return "";
}

struct IqMeta {
  IqDatatype datatype = IqDatatype::cu8;
  double sample_rate_sps = 0.0;
  double center_freq_hz = 0.0;
};

class IIqSource {
 public:
  virtual ~IIqSource() = default;

  virtual const IqMeta& meta() const = 0;

  // Fills out with up to out.size() cf32 samples and returns how many it wrote. 0 means end of
  // file (never while looping, unless the file holds no samples) or a read error (then failed()
  // is true). Blocking file I/O, not paced: call it only from the pipeline's reader thread.
  // `out` must not be empty: an empty span also returns 0 and would look like end of file.
  // Samples are passed through as stored: cf32_le files may hold NaN or inf.
  virtual std::size_t read(std::span<Cf32> out) = 0;

  virtual bool failed() const = 0;
};

struct IqOpenError {
  std::string code;     // messages.md error code: "not_found" or "bad_format"
  std::string message;  // for humans
};

using IqOpenResult = std::variant<std::unique_ptr<IIqSource>, IqOpenError>;

// Opens a .sigmf-meta (with its .sigmf-data) or a .wav IQ file. `path` has already passed the
// data-root checks (messages.md `play`). With `loop`, read() restarts at the end of the file.
IqOpenResult open_iq_file(const std::filesystem::path& path, bool loop);

}  // namespace bl
