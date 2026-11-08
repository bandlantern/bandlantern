#include "app/playback_controller.hpp"

#include <utility>

namespace bl {
namespace {

std::string source_kind(const std::filesystem::path& path) {
  return path.extension() == ".wav" ? "wav" : "sigmf";
}

}  // namespace

std::variant<std::filesystem::path, api::ControlError> resolve_play_path(const std::filesystem::path& data_root,
                                                                         const std::string& path) {
  namespace fs = std::filesystem;
  const fs::path rel(path);
  if (path.empty() || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
    return api::ControlError{"bad_path", "path must be relative to the data root"};
  }
  for (const auto& part : rel) {
    if (part == "..") return api::ControlError{"bad_path", "path must not contain .."};
  }
  if (rel.extension() != ".sigmf-meta" && rel.extension() != ".wav") {
    return api::ControlError{"bad_path", "path must name a .sigmf-meta or .wav file"};
  }

  std::error_code ec;
  const fs::path root = fs::canonical(data_root, ec);
  if (ec) return api::ControlError{"not_found", "data root does not exist"};
  const fs::path full = fs::canonical(root / rel, ec);
  if (ec) return api::ControlError{"not_found", path + " does not exist"};

  // canonical() resolved symlinks; the result must still be inside the root.
  const fs::path inside = full.lexically_relative(root);
  if (inside.empty() || *inside.begin() == "..") {
    return api::ControlError{"bad_path", "path leaves the data root"};
  }
  return full;
}

PlaybackController::PlaybackController(std::filesystem::path data_root, IqOpener open, IPipeline& pipeline,
                                       api::IEngineEvents& events)
    : data_root_(std::move(data_root)), open_(std::move(open)), pipeline_(pipeline), events_(events) {}

PlaybackController::~PlaybackController() {
  std::lock_guard command(command_mutex_);
  pipeline_.stop();
}

api::ControlResult PlaybackController::get_info() {
  std::lock_guard state(state_mutex_);
  return info_locked();
}

api::ControlResult PlaybackController::play(const std::string& path, bool loop) {
  auto resolved = resolve_play_path(data_root_, path);
  if (auto* error = std::get_if<api::ControlError>(&resolved)) return *error;

  std::lock_guard command(command_mutex_);
  pipeline_.stop();
  Playing playing{std::get<std::filesystem::path>(resolved), {}};
  playing.info.kind = source_kind(playing.file);
  playing.info.path = path;
  playing.info.loop = loop;
  if (auto error = start(playing)) return *error;
  return get_info();
}

api::ControlResult PlaybackController::stop() {
  std::lock_guard command(command_mutex_);
  pipeline_.stop();
  std::lock_guard state(state_mutex_);
  playing_.reset();
  return info_locked();
}

api::ControlResult PlaybackController::set_fft(std::optional<std::uint32_t> size, std::optional<double> rows_per_s) {
  std::lock_guard command(command_mutex_);
  std::optional<Playing> playing;
  {
    std::lock_guard state(state_mutex_);
    if (size) config_.fft_size = *size;
    if (rows_per_s) config_.rows_per_s = *rows_per_s;
    playing = playing_;
  }
  // messages.md `set_fft`: a playing file restarts from its start with the new settings.
  if (playing) {
    pipeline_.stop();
    if (auto error = start(*playing)) return *error;
  }
  return get_info();
}

std::optional<api::ControlError> PlaybackController::start(const Playing& playing) {
  IqOpenResult opened = open_(playing.file, playing.info.loop);
  if (auto* error = std::get_if<IqOpenError>(&opened)) {
    std::lock_guard state(state_mutex_);
    playing_.reset();
    return api::ControlError{error->code, error->message};
  }
  std::unique_ptr<IIqSource> source = std::move(std::get<std::unique_ptr<IIqSource>>(opened));

  Playing now = playing;
  now.info.datatype = std::string(datatype_name(source->meta().datatype));
  now.info.rate_sps = source->meta().sample_rate_sps;
  now.info.center_freq_hz = source->meta().center_freq_hz;
  PipelineConfig config;
  {
    std::lock_guard state(state_mutex_);
    playing_ = now;
    config = config_;
  }
  pipeline_.start(std::move(source), config, *this);
  return std::nullopt;
}

api::EngineInfo PlaybackController::info_locked() const {
  api::EngineInfo info;
  info.streaming = playing_.has_value();
  info.fft_size = config_.fft_size;
  info.rows_per_s = config_.rows_per_s;
  if (playing_) info.source = playing_->info;
  return info;
}

void PlaybackController::on_row(const SpectrumRow& row) { events_.on_row(row); }

void PlaybackController::on_end(PipelineEnd reason, std::string_view message) {
  api::EngineInfo info;
  {
    std::lock_guard state(state_mutex_);
    playing_.reset();
    info = info_locked();
  }
  // messages.md `error` `source_error`: the error, then the info that shows the source gone.
  if (reason == PipelineEnd::read_error) events_.on_error(api::ControlError{"source_error", std::string(message)});
  events_.on_info(info);
}

}  // namespace bl
