// Engine control surface behind the JSON commands (docs/api/messages.md). The server parses
// and range-checks each command, then calls these on its command thread, one at a time.
// A fake implements this in server tests; PlaybackController is the real one.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "pipeline/pipeline.hpp"

namespace bl::api {

using bl::SpectrumRow;

// messages.md `info.source`.
struct SourceInfo {
  std::string kind;      // "sigmf", "wav"
  std::string path;      // relative to the data root
  std::string datatype;  // "cu8", "cs16_le", "cf32_le"
  double rate_sps = 0.0;
  double center_freq_hz = 0.0;
  bool loop = false;
};

// Payload of the `info` event.
struct EngineInfo {
  bool streaming = false;
  std::uint32_t fft_size = 2048;
  double rows_per_s = 30.0;
  std::optional<SourceInfo> source;  // nullopt -> JSON null
};

// code is one of the messages.md error codes.
struct ControlError {
  std::string code;
  std::string message;
};

using ControlResult = std::variant<EngineInfo, ControlError>;

class IController {
 public:
  virtual ~IController() = default;

  virtual ControlResult get_info() = 0;
  // path as the client sent it; the controller checks it against the data root (bad_path).
  virtual ControlResult play(const std::string& path, bool loop) = 0;
  virtual ControlResult stop() = 0;
  // Range-checked by the server; nullopt = keep the current value.
  virtual ControlResult set_fft(std::optional<std::uint32_t> size,
                                std::optional<double> rows_per_s) = 0;
};

// What the controller pushes to clients: rows (frames.md) and unsolicited events (messages.md
// `info`, `error`), for every authenticated socket. Implemented by the server. None of these
// block.
class IEngineEvents {
 public:
  virtual ~IEngineEvents() = default;
  // Wait-free, so it is safe on the pipeline's DSP thread. One caller at a time: the per-socket
  // queues are single-producer (a new pipeline run may use a new thread once the old one joined).
  virtual void on_row(const SpectrumRow& row) = 0;
  // Any thread.
  virtual void on_info(const EngineInfo& info) = 0;
  virtual void on_error(const ControlError& error) = 0;
};

}  // namespace bl::api
