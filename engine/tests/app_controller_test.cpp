// PlaybackController against a fake pipeline, fake events, and a fake file opener (fakes only:
// the real pipeline is the owner's A0 work).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api/token.hpp"
#include "app/playback_controller.hpp"

using namespace bl;
namespace fs = std::filesystem;

namespace {

class FakeSource : public IIqSource {
 public:
  explicit FakeSource(IqMeta meta) : meta_(meta) {}
  const IqMeta& meta() const override { return meta_; }
  std::size_t read(std::span<Cf32>) override { return 0; }
  bool failed() const override { return false; }

 private:
  IqMeta meta_;
};

// Records what the controller asks for. The test thread plays the pipeline's threads by calling
// the sink it was given.
class FakePipeline : public IPipeline {
 public:
  void start(std::unique_ptr<IIqSource> source, const PipelineConfig& config, IPipelineSink& sink) override {
    REQUIRE_FALSE(running);  // the controller must stop first
    running = true;
    ++starts;
    last_config = config;
    last_meta = source->meta();
    this->sink = &sink;
  }
  void stop() override {
    running = false;
    ++stops;
  }

  bool running = false;
  int starts = 0;
  int stops = 0;
  PipelineConfig last_config;
  IqMeta last_meta;
  IPipelineSink* sink = nullptr;
};

class FakeEvents : public api::IEngineEvents {
 public:
  void on_row(const SpectrumRow& row) override {
    std::lock_guard lock(mutex);
    row_sample_indexes.push_back(row.sample_index);
  }
  void on_info(const api::EngineInfo& info) override {
    std::lock_guard lock(mutex);
    infos.push_back(info);
    log.push_back("info");
  }
  void on_error(const api::ControlError& error) override {
    std::lock_guard lock(mutex);
    errors.push_back(error);
    log.push_back("error");
  }

  std::mutex mutex;
  std::vector<std::uint64_t> row_sample_indexes;
  std::vector<api::EngineInfo> infos;
  std::vector<api::ControlError> errors;
  std::vector<std::string> log;
};

// A data root of its own (test cases may run in parallel), with a file outside it.
struct DataRoot {
  DataRoot() {
    fs::create_directories(root / "synth");
    std::ofstream(root / "synth" / "tones.sigmf-meta") << "{}";
    std::ofstream(root / "speech.wav") << "RIFF";
    std::ofstream(root / "notes.txt") << "x";
    std::ofstream(base / "outside.sigmf-meta") << "{}";
  }
  ~DataRoot() { fs::remove_all(base); }

  fs::path base = fs::temp_directory_path() / ("bl_root_" + api::generate_token().substr(0, 12));
  fs::path root = base / "data";
};

IqOpener fake_opener(std::vector<std::pair<fs::path, bool>>& opened) {
  return [&opened](const fs::path& path, bool loop) -> IqOpenResult {
    opened.emplace_back(path, loop);
    if (path.filename() == "speech.wav") return IqOpenError{"bad_format", "not IQ"};
    return std::make_unique<FakeSource>(IqMeta{IqDatatype::cu8, 2'400'000.0, 100'000'000.0});
  };
}

api::EngineInfo as_info(const api::ControlResult& result) {
  REQUIRE(std::holds_alternative<api::EngineInfo>(result));
  return std::get<api::EngineInfo>(result);
}

std::string error_code(const api::ControlResult& result) {
  REQUIRE(std::holds_alternative<api::ControlError>(result));
  return std::get<api::ControlError>(result).code;
}

struct Rig {
  DataRoot data;
  std::vector<std::pair<fs::path, bool>> opened;
  FakePipeline pipeline;
  FakeEvents events;
  PlaybackController ctl{data.root, fake_opener(opened), pipeline, events};
};

}  // namespace

TEST_CASE("resolve_play_path refuses paths outside the data root", "[app][controller]") {
  DataRoot d;
  CHECK(std::holds_alternative<fs::path>(resolve_play_path(d.root, "synth/tones.sigmf-meta")));
  const auto code = [&](const std::string& p) {
    const auto r = resolve_play_path(d.root, p);
    REQUIRE(std::holds_alternative<api::ControlError>(r));
    return std::get<api::ControlError>(r).code;
  };
  CHECK(code("") == "bad_path");
  CHECK(code((d.root / "synth" / "tones.sigmf-meta").string()) == "bad_path");  // absolute
  CHECK(code("../outside.sigmf-meta") == "bad_path");
  CHECK(code("synth/../../outside.sigmf-meta") == "bad_path");
  CHECK(code("notes.txt") == "bad_path");
  CHECK(code("synth/none.sigmf-meta") == "not_found");

  fs::create_symlink(d.base / "outside.sigmf-meta", d.root / "link.sigmf-meta");
  CHECK(code("link.sigmf-meta") == "bad_path");
}

TEST_CASE("play opens the file, starts the pipeline, and reports the source", "[app][controller]") {
  Rig r;
  const api::EngineInfo info = as_info(r.ctl.play("synth/tones.sigmf-meta", true));
  CHECK(info.streaming);
  REQUIRE(info.source);
  CHECK(info.source->kind == "sigmf");
  CHECK(info.source->path == "synth/tones.sigmf-meta");
  CHECK(info.source->datatype == "cu8");
  CHECK(info.source->rate_sps == 2'400'000.0);
  CHECK(info.source->center_freq_hz == 100'000'000.0);
  CHECK(info.source->loop);
  CHECK(r.pipeline.running);
  CHECK(r.pipeline.last_config.fft_size == 2048);
  REQUIRE(r.opened.size() == 1);
  CHECK(r.opened[0].first == fs::canonical(r.data.root / "synth" / "tones.sigmf-meta"));
  CHECK(r.opened[0].second);
}

TEST_CASE("a refused play leaves the current playback alone", "[app][controller]") {
  Rig r;
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  CHECK(error_code(r.ctl.play("../outside.sigmf-meta", false)) == "bad_path");
  CHECK(error_code(r.ctl.play("synth/none.sigmf-meta", false)) == "not_found");
  CHECK(r.pipeline.running);
  CHECK(as_info(r.ctl.get_info()).source->path == "synth/tones.sigmf-meta");
}

TEST_CASE("a file that fails to open stops playback and is reported", "[app][controller]") {
  Rig r;
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  CHECK(error_code(r.ctl.play("speech.wav", false)) == "bad_format");
  CHECK_FALSE(r.pipeline.running);
  const api::EngineInfo info = as_info(r.ctl.get_info());
  CHECK_FALSE(info.streaming);
  CHECK_FALSE(info.source);
}

TEST_CASE("stop stops the pipeline; not an error when idle", "[app][controller]") {
  Rig r;
  CHECK_FALSE(as_info(r.ctl.stop()).streaming);
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  const api::EngineInfo info = as_info(r.ctl.stop());
  CHECK_FALSE(info.streaming);
  CHECK_FALSE(info.source);
  CHECK_FALSE(r.pipeline.running);
}

TEST_CASE("set_fft keeps missing fields and restarts a playing file", "[app][controller]") {
  Rig r;
  const api::EngineInfo idle = as_info(r.ctl.set_fft(4096, std::nullopt));
  CHECK(idle.fft_size == 4096);
  CHECK(idle.rows_per_s == 30.0);
  CHECK(r.pipeline.starts == 0);  // nothing playing: nothing to start

  as_info(r.ctl.play("synth/tones.sigmf-meta", true));
  CHECK(r.pipeline.last_config.fft_size == 4096);
  const api::EngineInfo info = as_info(r.ctl.set_fft(std::nullopt, 10.0));
  CHECK(info.fft_size == 4096);
  CHECK(info.rows_per_s == 10.0);
  CHECK(info.streaming);
  CHECK(r.pipeline.starts == 2);
  CHECK(r.pipeline.last_config.rows_per_s == 10.0);
  CHECK(r.opened.size() == 2);  // reopened from the start
  CHECK(r.opened[1].second);    // with the same loop setting
}

TEST_CASE("rows pass straight through to the events", "[app][controller]") {
  Rig r;
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  const std::vector<float> bins(8, -80.0f);
  SpectrumRow row;
  row.sample_index = 4096;
  row.dbfs = bins;
  r.pipeline.sink->on_row(row);
  CHECK(r.events.row_sample_indexes == std::vector<std::uint64_t>{4096});
}

TEST_CASE("end of file: unsolicited info without the source", "[app][controller]") {
  Rig r;
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  std::thread pipeline_thread([&] { r.pipeline.sink->on_end(PipelineEnd::end_of_file, ""); });
  pipeline_thread.join();

  CHECK(r.events.log == std::vector<std::string>{"info"});
  CHECK_FALSE(r.events.infos[0].streaming);
  CHECK_FALSE(r.events.infos[0].source);
  CHECK_FALSE(as_info(r.ctl.get_info()).streaming);
}

TEST_CASE("read error: source_error, then info", "[app][controller]") {
  Rig r;
  as_info(r.ctl.play("synth/tones.sigmf-meta", false));
  std::thread pipeline_thread([&] { r.pipeline.sink->on_end(PipelineEnd::read_error, "short read"); });
  pipeline_thread.join();

  CHECK(r.events.log == std::vector<std::string>{"error", "info"});
  CHECK(r.events.errors[0].code == "source_error");
  CHECK(r.events.errors[0].message == "short read");
}

// The pipeline's stop() joins a thread that may be inside on_end; the controller must not hold a
// lock on_end needs while it calls stop().
TEST_CASE("stop can join a pipeline thread that is inside on_end", "[app][controller]") {
  DataRoot data;
  std::vector<std::pair<fs::path, bool>> opened;
  FakeEvents events;

  class JoiningPipeline : public IPipeline {
   public:
    void start(std::unique_ptr<IIqSource>, const PipelineConfig&, IPipelineSink& sink) override {
      thread_ = std::thread([&sink] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sink.on_end(PipelineEnd::end_of_file, "");
      });
    }
    void stop() override {
      if (thread_.joinable()) thread_.join();
    }

   private:
    std::thread thread_;
  } pipeline;

  PlaybackController ctl{data.root, fake_opener(opened), pipeline, events};
  as_info(ctl.play("synth/tones.sigmf-meta", false));
  CHECK_FALSE(as_info(ctl.stop()).streaming);  // would deadlock if stop() held the state lock
  std::lock_guard lock(events.mutex);
  CHECK(events.log == std::vector<std::string>{"info"});
}
