// Engine entry point: parses options and wires the server <-> playback controller.
// Options: docs/api/README.md (Transport, Token) and messages.md (`--data-root`).
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include "api/server.hpp"
#include "api/token.hpp"
#include "app/playback_controller.hpp"

namespace {

constexpr const char* kEngineVersion = "0.0.0";

std::atomic<bool> g_stop_requested{false};  // set from the signal handler only

void on_signal(int) { g_stop_requested.store(true); }

// Until the owner's A0 pipeline exists (tutor mode, CURRICULUM.md A0) the engine cannot play:
// every `play` gets `not_available`, and this pipeline is never started. Fakes only, no stub DSP.
// A0 replaces both with open_iq_file and the real pipeline.
class NoPipeline : public bl::IPipeline {
 public:
  void start(std::unique_ptr<bl::IIqSource>, const bl::PipelineConfig&, bl::IPipelineSink&) override {}
  void stop() override {}
};

bl::IqOpenResult open_not_available(const std::filesystem::path&, bool) {
  return bl::IqOpenError{"not_available", "this engine build has no playback pipeline yet (A0)"};
}

struct Options {
  bl::api::ServerConfig server;
  std::filesystem::path data_root = "testdata";
  bool print_url = false;
};

void print_usage() {
  std::fputs(
      "usage: bandlantern-engine [--port N] [--dev-origin ORIGIN]... [--data-root DIR]\n"
      "                          [--static-root DIR] [--print-url] [--version]\n",
      stderr);
}

// Returns false on a bad or unknown option.
bool parse_options(int argc, char** argv, Options& options) {
  options.server.static_root = "ui/dist";
  options.server.engine_version = kEngineVersion;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--print-url") {
      options.print_url = true;
    } else if (arg == "--port" && has_value) {
      const int port = std::atoi(argv[++i]);
      if (port < 1 || port > 65535) return false;
      options.server.port = static_cast<std::uint16_t>(port);
    } else if (arg == "--dev-origin" && has_value) {
      options.server.dev_origins.emplace_back(argv[++i]);
    } else if (arg == "--data-root" && has_value) {
      options.data_root = argv[++i];
    } else if (arg == "--static-root" && has_value) {
      options.server.static_root = argv[++i];
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view{argv[1]} == "--version") {
    std::printf("bandlantern-engine %s\n", kEngineVersion);
    return 0;
  }
  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage();
    return 2;
  }
  options.server.token = bl::api::generate_token();

  // The server is the controller's IEngineEvents and the controller serves the server's
  // commands, so the controller is handed over at start().
  bl::api::Server server(options.server);
  NoPipeline pipeline;
  bl::PlaybackController controller(options.data_root, open_not_available, pipeline, server);
  try {
    server.start(controller);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "bandlantern-engine: cannot listen on 127.0.0.1:%u: %s\n",
                 static_cast<unsigned>(options.server.port), e.what());
    return 1;
  }

  // README "Bootstrap": the URL carries the token, so it goes to our own stderr only.
  if (options.print_url) {
    std::fprintf(stderr, "http://127.0.0.1:%u/#token=%s\n", static_cast<unsigned>(server.port()),
                 options.server.token.c_str());
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  while (!g_stop_requested.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Stop the command thread first: a command still running may be calling the controller.
  server.stop();
  controller.stop();  // joins the pipeline's threads
  return 0;
}
