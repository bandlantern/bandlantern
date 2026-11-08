// The seam between the bench plumbing and the owner's A0 playback pipeline (tutor mode,
// docs/learning/CURRICULUM.md A0). The plumbing opens the source and consumes rows; everything
// between (threads, the SPSC ring, pacing, the spectrum block) is the pipeline's.
// Tests drive the controller with a fake IPipeline; the real one arrives with A0.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "io/iq_source.hpp"

namespace bl {

struct PipelineConfig {
  std::uint32_t fft_size = 2048;  // power of two, 256..16384 (checked by the server)
  double rows_per_s = 30.0;       // 1..60 (checked by the server)
};

// One finished spectrum row. `dbfs` points at pipeline-owned memory that is valid only during
// IPipelineSink::on_row(); the sink copies what it keeps. Meaning of each field: frames.md.
struct SpectrumRow {
  std::uint64_t sample_index = 0;  // first source sample in this row, counted from start()
  double center_freq_hz = 0.0;     // RF frequency of bin dbfs.size() / 2
  double rate_sps = 0.0;           // complex sample rate the FFT ran at
  std::span<const float> dbfs;     // FFT-shifted (DC at index size/2), every value finite
};

enum class PipelineEnd {
  end_of_file,  // the source ran out (no loop)
  read_error,   // the source failed (IIqSource::failed())
};

// Implemented by the plumbing (the controller). Both calls come from pipeline threads and must
// not block: no locks held across I/O, no waiting on clients (frames.md "Back-pressure").
class IPipelineSink {
 public:
  virtual ~IPipelineSink() = default;
  virtual void on_row(const SpectrumRow& row) = 0;
  // Once, after the last on_row(), when the pipeline ends by itself. Not called for stop().
  virtual void on_end(PipelineEnd reason, std::string_view message) = 0;
};

// Implemented by the owner (A0). Called from the controller thread, one call at a time.
class IPipeline {
 public:
  virtual ~IPipeline() = default;

  // Takes the source and starts. Reads are paced to real time at
  // source->meta().sample_rate_sps; rows go to `sink`, which outlives the run.
  // Precondition: not running (the controller calls stop() first).
  virtual void start(std::unique_ptr<IIqSource> source, const PipelineConfig& config,
                     IPipelineSink& sink) = 0;

  // Stops and joins the pipeline's threads. After it returns the sink gets no more calls.
  // Safe to call when not running, and after on_end().
  virtual void stop() = 0;
};

}  // namespace bl
