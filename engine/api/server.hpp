// Engine local server: HTTP static files + WebSocket on 127.0.0.1 (docs/api/README.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "api/controller.hpp"

namespace bl::api {

inline constexpr const char* kApiVersion = "0.1";  // docs/api/README.md "Versioning"

struct ServerConfig {
  std::string token;                     // from generate_token(); never logged
  std::uint16_t port = 8780;             // 0 = ephemeral (tests); see Server::port()
  std::vector<std::string> dev_origins;  // extra allowed Origins, e.g. "http://localhost:5173"
  std::filesystem::path static_root;     // UI bundle (ui/dist); empty = no static files
  std::string engine_version = "0.0.0";
  int auth_timeout_ms = 2000;            // hello deadline after the upgrade (ADR-008)
  int pump_interval_ms = 5;              // how often idle sockets poll their row queue
  int drop_event_interval_ms = 1000;     // minimum spacing of `drop` events per socket
  int ws_idle_timeout_ms = 30000;        // silent peer: ping at half, drop the socket at this
};

class Server : public IEngineEvents {
 public:
  static constexpr int kMaxSockets = 8;  // authenticated sockets; each preallocates ~0.5 MB

  explicit Server(ServerConfig config);
  ~Server() override;
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Binds 127.0.0.1:port and starts the I/O and command threads. Throws on bind failure.
  // The controller is passed here, not to the constructor, because the controller needs this
  // server as its IEngineEvents. It must outlive stop().
  void start(IController& controller);
  void stop();
  std::uint16_t port() const;

  // IEngineEvents. on_row copies the row into every authenticated socket's queue (dropping it
  // for sockets whose queue is full); rows with 0 or more than kMaxBins bins are ignored.
  void on_row(const SpectrumRow& row) noexcept override;
  void on_info(const EngineInfo& info) override;
  void on_error(const ControlError& error) override;

  struct Impl;  // public only so the .cpp's Session class can name it; opaque to callers

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace bl::api
