// The real IController: checks `play` paths against the data root, opens the file, and runs
// the playback pipeline (the owner's A0 IPipeline; a fake in tests). Rows go straight through
// to the server; the pipeline's end becomes the unsolicited `info` / `error` (messages.md).
#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

#include "api/controller.hpp"
#include "io/iq_source.hpp"
#include "pipeline/pipeline.hpp"

namespace bl {

// Opens an IQ file; open_iq_file() in the engine, a fake in tests.
using IqOpener = std::function<IqOpenResult(const std::filesystem::path& path, bool loop)>;

// messages.md `play.path`: relative, no `..`, names a .sigmf-meta or .wav file, and does not leave
// the data root through symlinks. Returns the resolved file or the error to reply with.
std::variant<std::filesystem::path, api::ControlError> resolve_play_path(const std::filesystem::path& data_root,
                                                                         const std::string& path);

class PlaybackController : public api::IController, public IPipelineSink {
 public:
  // pipeline and events must outlive the controller.
  PlaybackController(std::filesystem::path data_root, IqOpener open, IPipeline& pipeline,
                     api::IEngineEvents& events);
  ~PlaybackController() override;  // stops the pipeline
  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  // IController (the server's command thread).
  api::ControlResult get_info() override;
  api::ControlResult play(const std::string& path, bool loop) override;
  api::ControlResult stop() override;
  api::ControlResult set_fft(std::optional<std::uint32_t> size, std::optional<double> rows_per_s) override;

  // IPipelineSink (pipeline threads).
  void on_row(const SpectrumRow& row) override;
  void on_end(PipelineEnd reason, std::string_view message) override;

 private:
  // A file that is playing (or was, until on_end): what set_fft needs to start it again.
  struct Playing {
    std::filesystem::path file;  // resolved
    api::SourceInfo info;        // as reported; path as the client sent it
  };

  api::EngineInfo info_locked() const;
  // Opens playing->file and starts the pipeline on it. Caller holds command_mutex_ and has
  // stopped the pipeline. On error nothing runs and the state shows no source.
  std::optional<api::ControlError> start(const Playing& playing);

  const std::filesystem::path data_root_;
  const IqOpener open_;
  IPipeline& pipeline_;
  api::IEngineEvents& events_;

  // Held across IPipeline calls (start/stop may join threads). on_end never takes it, so
  // stop() can join a thread that is inside on_end.
  std::mutex command_mutex_;
  // Guards the fields below. Never held while calling the pipeline or the events.
  mutable std::mutex state_mutex_;
  PipelineConfig config_;
  std::optional<Playing> playing_;  // nullopt: nothing playing
};

}  // namespace bl
