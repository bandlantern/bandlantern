#include "api/server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <string_view>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include "api/channel.hpp"
#include "api/token.hpp"

namespace bl::api {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace detail {
class Session;
}

constexpr std::size_t kMaxTextBytes = 65536;  // README "Transport"
constexpr std::size_t kReadChunkBytes = 4096;

struct Server::Impl {
  explicit Impl(ServerConfig c)
      : cfg(std::move(c)), channels(new Channel[kMaxSockets]), acceptor(ioc), pump_timer(ioc) {}

  ServerConfig cfg;
  IController* ctl = nullptr;                                          // set by start()
  std::unique_ptr<Channel[]> channels;                                 // preallocated, never freed early
  std::array<std::shared_ptr<detail::Session>, kMaxSockets> sessions;  // I/O thread only
  net::io_context ioc{1};
  tcp::acceptor acceptor;
  net::steady_timer pump_timer;
  std::thread thread;
  // Commands run here, one at a time in arrival order, so a slow controller call (opening a
  // file, restarting the pipeline) never stalls the I/O thread and the frames it writes.
  net::io_context commands{1};
  net::executor_work_guard<net::io_context::executor_type> commands_work{commands.get_executor()};
  std::thread command_thread;
  std::set<std::string> allowed_hosts;    // lowercase
  std::set<std::string> allowed_origins;  // lowercase
  std::filesystem::path root_canon;

  void accept_next();
  void schedule_pump();
  void broadcast(const json& j);
};

namespace detail {

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

json info_to_json(const EngineInfo& info, std::optional<std::int64_t> re) {
  json j = {{"v", 1},
            {"type", "info"},
            {"streaming", info.streaming},
            {"fft_size", info.fft_size},
            {"rows_per_s", info.rows_per_s},
            {"source", nullptr}};
  if (re) j["re"] = *re;
  if (info.source) {
    j["source"] = {{"kind", info.source->kind},
                   {"path", info.source->path},
                   {"datatype", info.source->datatype},
                   {"rate_sps", info.source->rate_sps},
                   {"center_freq_hz", info.source->center_freq_hz},
                   {"loop", info.source->loop}};
  }
  return j;
}

json error_json(std::optional<std::int64_t> re, const std::string& code, const std::string& msg) {
  json j = {{"v", 1}, {"type", "error"}, {"code", code}, {"message", msg}};
  if (re) j["re"] = *re;
  return j;
}

const char* mime_for(const std::filesystem::path& p) {
  const std::string ext = lower(p.extension().string());
  static const std::pair<const char*, const char*> kTable[] = {
      {".html", "text/html; charset=utf-8"},      {".js", "text/javascript; charset=utf-8"},
      {".mjs", "text/javascript; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
      {".json", "application/json"},             {".svg", "image/svg+xml"},
      {".png", "image/png"},                     {".ico", "image/x-icon"},
      {".wasm", "application/wasm"},             {".txt", "text/plain; charset=utf-8"},
      {".map", "application/json"}};
  for (const auto& [e, m] : kTable)
    if (ext == e) return m;
  return "application/octet-stream";
}

// One TCP connection: first an HTTP request, which is either a static-file GET or the /ws
// upgrade; after the upgrade the same object runs the WebSocket protocol.
// All members are touched only on the server's single I/O thread.
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(Server::Impl& srv, tcp::socket socket)
      : srv_(srv), ws_(std::move(socket)), auth_timer_(ws_.get_executor()) {}

  ~Session() {
    if (channel_idx_ >= 0) {  // only reached at server shutdown
      srv_.channels[channel_idx_].deactivate();
      srv_.channels[channel_idx_].in_use = false;
    }
  }

  void run() {
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
    http::async_read(ws_.next_layer(), buffer_, req_,
                     [self = shared_from_this()](beast::error_code ec, std::size_t) {
                       if (!ec) self->on_http_request();
                     });
  }

  // Called by the pump timer.
  void tick(std::chrono::steady_clock::time_point now) {
    if (!authed_ || closing_) return;
    const std::uint64_t dropped = srv_.channels[channel_idx_].dropped();
    const auto spacing = std::chrono::milliseconds(srv_.cfg.drop_event_interval_ms);
    if (dropped > dropped_reported_ && now - last_drop_event_ >= spacing) {
      send_json({{"v", 1}, {"type", "drop"}, {"rows_dropped", dropped - dropped_reported_}});
      dropped_reported_ = dropped;
      last_drop_event_ = now;
    }
    pump_writes();
  }

  bool authed() const { return authed_; }

  void send_json(const json& j) {
    if (closing_) return;
    out_text_.push_back(j.dump());
    pump_writes();
  }

 private:
  // ---------------- HTTP ----------------

  void respond_status(http::status status) {
    http::response<http::empty_body> res{status, req_.version()};
    res.keep_alive(false);
    res.prepare_payload();
    send_http(std::move(res));
  }

  void send_http(http::message_generator gen) {
    beast::async_write(ws_.next_layer(), std::move(gen),
                       [self = shared_from_this()](beast::error_code, std::size_t) {
                         beast::error_code ignored;
                         self->ws_.next_layer().socket().shutdown(tcp::socket::shutdown_send, ignored);
                       });
  }

  void on_http_request() {
    // The Host check blocks DNS rebinding (README "Request checks").
    if (!srv_.allowed_hosts.contains(lower(req_[http::field::host])))
      return respond_status(http::status::forbidden);

    const bool upgrade = websocket::is_upgrade(req_);
    const std::string target = std::string(req_.target());
    const bool has_origin = req_.find(http::field::origin) != req_.end();
    const std::string origin = lower(req_[http::field::origin]);

    if (upgrade) {
      if (target != "/ws") return respond_status(http::status::forbidden);
      if (!has_origin || !srv_.allowed_origins.contains(origin))
        return respond_status(http::status::forbidden);
      return start_websocket();
    }
    if (target == "/ws") return respond_status(http::status::bad_request);
    if (has_origin && !srv_.allowed_origins.contains(origin))
      return respond_status(http::status::forbidden);
    serve_static(target);
  }

  void serve_static(std::string target) {
    if (req_.method() != http::verb::get || srv_.root_canon.empty())
      return respond_status(http::status::forbidden);

    const std::size_t cut = target.find_first_of("?#");
    if (cut != std::string::npos) target.resize(cut);

    // No percent-decoding is done, so any encoded byte is simply refused; backslashes and NUL
    // are refused for Windows/C-string safety; ".." segments are refused outright.
    const bool bad_chars = target.empty() || target[0] != '/' ||
                           target.find_first_of(std::string("%\\\0", 3)) != std::string::npos;
    if (bad_chars) return respond_status(http::status::forbidden);
    std::string rel = target.substr(1);
    for (std::size_t pos = 0; pos <= rel.size();) {
      const std::size_t end = std::min(rel.find('/', pos), rel.size());
      if (rel.substr(pos, end - pos) == "..") return respond_status(http::status::forbidden);
      pos = end + 1;
    }
    if (rel.empty() || rel.back() == '/') rel += "index.html";

    // Second line of defence: resolve symlinks and require the result inside the root.
    std::error_code fec;
    const auto resolved = std::filesystem::weakly_canonical(srv_.root_canon / rel, fec);
    const auto& root = srv_.root_canon;
    if (fec || std::mismatch(root.begin(), root.end(), resolved.begin(), resolved.end()).first != root.end())
      return respond_status(http::status::forbidden);
    if (!std::filesystem::is_regular_file(resolved, fec)) return respond_status(http::status::not_found);

    http::file_body::value_type body;
    beast::error_code ec;
    body.open(resolved.string().c_str(), beast::file_mode::scan, ec);
    if (ec) return respond_status(http::status::not_found);

    http::response<http::file_body> res{http::status::ok, req_.version()};
    res.set(http::field::content_type, mime_for(resolved));
    res.set(http::field::x_content_type_options, "nosniff");
    res.set(http::field::cache_control, "no-store");
    // Only our own files may run, connect, or frame us: script injected into the UI could
    // otherwise drive the engine with the page's token. Styles allow inline: the theme CSS is
    // inserted at run time. 'self' covers ws:// to this host and port (CSP Level 3).
    res.set(http::field::content_security_policy,
            "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
            "connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
    res.set(http::field::referrer_policy, "no-referrer");
    res.body() = std::move(body);
    res.keep_alive(false);
    res.prepare_payload();
    send_http(std::move(res));
  }

  // ---------------- WebSocket ----------------

  void start_websocket() {
    beast::get_lowest_layer(ws_).expires_never();
    ws_.read_message_max(0);  // we enforce the 65536 limit ourselves to close with 1002, not 1009
    // Pings a silent peer at half the idle timeout and drops it at the timeout, so a vanished
    // client cannot hold a stuck write and a socket slot forever (README "Transport").
    ws_.set_option(websocket::stream_base::timeout{
        std::chrono::seconds(10), std::chrono::milliseconds(srv_.cfg.ws_idle_timeout_ms), true});
    buffer_.clear();
    ws_.async_accept(req_, [self = shared_from_this()](beast::error_code ec) {
      if (ec) return;
      self->start_auth_timer();
      self->read_more();
    });
  }

  void start_auth_timer() {
    auth_timer_.expires_after(std::chrono::milliseconds(srv_.cfg.auth_timeout_ms));
    auth_timer_.async_wait([self = shared_from_this()](beast::error_code ec) {
      if (!ec && !self->authed_) self->close_with(websocket::close_code::policy_error, "auth");
    });
  }

  void read_more() {
    ws_.async_read_some(buffer_, kReadChunkBytes,
                        [self = shared_from_this()](beast::error_code ec, std::size_t) { self->on_read(ec); });
  }

  void on_read(beast::error_code ec) {
    if (ec) return release_channel();  // closed, reset, or timed out
    if (closing_) return read_more();  // keep reading so the close handshake can finish

    if (ws_.got_binary()) return close_with(websocket::close_code::protocol_error, "binary");
    if (buffer_.size() > kMaxTextBytes) return close_with(websocket::close_code::protocol_error, "too big");
    if (ws_.is_message_done()) {
      const std::string msg = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      handle_message(msg);
    }
    read_more();
  }

  void close_with(websocket::close_code code, const char* reason) {
    if (closing_) return;
    closing_ = true;
    auth_timer_.cancel();
    release_channel();
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(3));
    ws_.async_close(websocket::close_reason(code, reason), [self = shared_from_this()](beast::error_code) {});
  }

  // Stops the producer feeding this socket and frees its slot for reuse. The slot is only
  // marked free (and the registry entry dropped) from a posted handler, so a new session
  // cannot be handed the slot while this one is still unwinding.
  void release_channel() {
    auth_timer_.cancel();
    if (channel_idx_ < 0) return;
    const int idx = channel_idx_;
    srv_.channels[idx].deactivate();
    channel_idx_ = -1;
    authed_ = false;
    net::post(ws_.get_executor(), [&srv = srv_, idx, keep = shared_from_this()] {
      srv.sessions[idx].reset();
      srv.channels[idx].in_use = false;
    });
  }

  void handle_message(const std::string& msg) {
    const json j = json::parse(msg, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return close_with(websocket::close_code::protocol_error, "json");
    if (!authed_) return handle_hello(j);
    net::post(srv_.commands, [self = shared_from_this(), j] { self->handle_command(j); });
  }

  // Command thread: hands a reply to the I/O thread. Posts from the one command thread run in
  // order, so replies on a socket keep command order (messages.md).
  void reply(json j) {
    net::post(ws_.get_executor(), [self = shared_from_this(), j = std::move(j)] { self->send_json(j); });
  }

  // ADR-008: the first message must be hello with the right token, else 1008 and no hint.
  void handle_hello(const json& j) {
    const auto type = j.find("type");
    const auto token = j.find("token");
    const bool ok = type != j.end() && type->is_string() && *type == "hello" && token != j.end() &&
                    token->is_string() && constant_time_equal(token->get<std::string>(), srv_.cfg.token);
    if (!ok) return close_with(websocket::close_code::policy_error, "auth");

    int idx = -1;
    for (int i = 0; i < Server::kMaxSockets; ++i) {
      if (!srv_.channels[i].in_use) {
        idx = i;
        break;
      }
    }
    if (idx < 0) return close_with(websocket::close_code::internal_error, "busy");

    auth_timer_.cancel();
    json reply = {{"v", 1}, {"type", "hello_ok"}, {"api_version", kApiVersion},
                  {"engine_version", srv_.cfg.engine_version}};
    if (const auto re = command_id(j)) reply["re"] = *re;

    channel_idx_ = idx;
    Channel& ch = srv_.channels[idx];
    ch.in_use = true;
    srv_.sessions[idx] = shared_from_this();
    dropped_reported_ = ch.dropped();
    authed_ = true;
    // Activate before replying: the reply can reach the client before send_json returns, and a
    // row published right after hello_ok must not be dropped by an inactive channel.
    ch.activate();
    send_json(reply);  // queued ahead of any frame: control text has priority in pump_writes
  }

  static std::optional<std::int64_t> command_id(const json& j) {
    const auto it = j.find("id");
    if (it == j.end() || !it->is_number_integer()) return std::nullopt;
    const std::int64_t id = it->get<std::int64_t>();
    if (id < 1 || id > 2147483647) return std::nullopt;
    return id;
  }

  // Command thread (Impl::commands): touches only srv_.ctl and reply().
  void handle_command(const json& j) {
    const auto id = command_id(j);
    const auto v = j.find("v");
    if (v == j.end() || !v->is_number_integer() || v->get<std::int64_t>() != 1)
      return reply(error_json(id, "bad_version", "unsupported envelope version"));
    const auto type_it = j.find("type");
    if (type_it == j.end() || !type_it->is_string() || !id)
      return reply(error_json(id, "bad_request", "type and id are required"));
    const std::string type = type_it->get<std::string>();

    ControlResult result;
    if (type == "get_info") {
      result = srv_.ctl->get_info();
    } else if (type == "stop") {
      result = srv_.ctl->stop();
    } else if (type == "play") {
      const auto path = j.find("path");
      const auto loop = j.find("loop");
      if (path == j.end() || !path->is_string() || (loop != j.end() && !loop->is_boolean()))
        return reply(error_json(id, "bad_request", "play needs string path, bool loop"));
      result = srv_.ctl->play(path->get<std::string>(), loop != j.end() && loop->get<bool>());
    } else if (type == "set_fft") {
      auto parsed = parse_set_fft(j, id);
      if (!parsed) return;
      result = *parsed;
    } else {
      return reply(error_json(id, "unknown_type", "unknown message type"));
    }

    if (const auto* err = std::get_if<ControlError>(&result)) return reply(error_json(id, err->code, err->message));
    reply(info_to_json(std::get<EngineInfo>(result), id));
  }

  // Validates set_fft fields (messages.md) and calls the controller; nullopt = error sent.
  std::optional<ControlResult> parse_set_fft(const json& j, std::optional<std::int64_t> id) {
    std::optional<std::uint32_t> size;
    std::optional<double> rows_per_s;
    if (const auto it = j.find("size"); it != j.end()) {
      if (!it->is_number_integer()) {
        reply(error_json(id, "bad_request", "size must be an integer"));
        return std::nullopt;
      }
      const std::int64_t s = it->get<std::int64_t>();
      if (s < 256 || s > 16384 || (s & (s - 1)) != 0) {
        reply(error_json(id, "bad_value", "size must be a power of two, 256..16384"));
        return std::nullopt;
      }
      size = static_cast<std::uint32_t>(s);
    }
    if (const auto it = j.find("rows_per_s"); it != j.end()) {
      if (!it->is_number()) {
        reply(error_json(id, "bad_request", "rows_per_s must be a number"));
        return std::nullopt;
      }
      const double r = it->get<double>();
      if (!(r >= 1.0 && r <= 60.0)) {
        reply(error_json(id, "bad_value", "rows_per_s must be 1..60"));
        return std::nullopt;
      }
      rows_per_s = r;
    }
    return srv_.ctl->set_fft(size, rows_per_s);
  }

  // One write at a time. Control text goes first, then queued spectrum frames. A frame stays in
  // its ring slot until the write completes, so the ring is the back-pressure queue.
  void pump_writes() {
    if (writing_ || closing_ || !authed_) return;
    if (!out_text_.empty()) {
      writing_ = true;
      writing_frame_ = false;
      ws_.text(true);
      ws_.async_write(net::buffer(out_text_.front()),
                      [self = shared_from_this()](beast::error_code ec, std::size_t) { self->on_write(ec); });
      return;
    }
    const Channel::Slot* slot = srv_.channels[channel_idx_].front();
    if (slot == nullptr) return;
    writing_ = true;
    writing_frame_ = true;
    ws_.binary(true);
    ws_.async_write(net::buffer(slot->bytes.data(), slot->len),
                    [self = shared_from_this()](beast::error_code ec, std::size_t) { self->on_write(ec); });
  }

  void on_write(beast::error_code ec) {
    writing_ = false;
    if (ec) return release_channel();
    if (channel_idx_ < 0) return;  // released while the write was in flight
    if (writing_frame_) {
      srv_.channels[channel_idx_].pop();
    } else {
      out_text_.pop_front();
    }
    pump_writes();
  }

  Server::Impl& srv_;
  websocket::stream<beast::tcp_stream> ws_;
  net::steady_timer auth_timer_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;

  bool authed_ = false;
  bool closing_ = false;
  bool writing_ = false;
  bool writing_frame_ = false;
  int channel_idx_ = -1;
  std::deque<std::string> out_text_;
  std::uint64_t dropped_reported_ = 0;
  std::chrono::steady_clock::time_point last_drop_event_{};
};

}  // namespace detail

void Server::Impl::accept_next() {
  acceptor.async_accept([this](beast::error_code ec, tcp::socket socket) {
    if (ec == net::error::operation_aborted) return;
    if (!ec) std::make_shared<detail::Session>(*this, std::move(socket))->run();
    accept_next();
  });
}

// Idle sockets (no write in flight) notice newly queued rows within one pump interval. A timer
// poll keeps the DSP thread free of any wake-up call into asio (which locks and allocates).
void Server::Impl::schedule_pump() {
  pump_timer.expires_after(std::chrono::milliseconds(cfg.pump_interval_ms));
  pump_timer.async_wait([this](beast::error_code ec) {
    if (ec) return;
    const auto now = std::chrono::steady_clock::now();
    for (auto& s : sessions)
      if (s) s->tick(now);
    schedule_pump();
  });
}

// I/O thread only.
void Server::Impl::broadcast(const json& j) {
  for (auto& s : sessions)
    if (s && s->authed()) s->send_json(j);
}

Server::Server(ServerConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Server::~Server() { stop(); }

void Server::start(IController& controller) {
  Impl& s = *impl_;
  s.ctl = &controller;
  const tcp::endpoint ep(net::ip::make_address_v4("127.0.0.1"), s.cfg.port);
  s.acceptor.open(ep.protocol());
  s.acceptor.set_option(net::socket_base::reuse_address(true));
  s.acceptor.bind(ep);
  s.acceptor.listen();

  const std::string p = std::to_string(port());
  s.allowed_hosts = {"127.0.0.1:" + p, "localhost:" + p};
  s.allowed_origins = {"http://127.0.0.1:" + p, "http://localhost:" + p};
  for (const auto& o : s.cfg.dev_origins) s.allowed_origins.insert(detail::lower(o));

  if (!s.cfg.static_root.empty()) {
    std::error_code ec;
    s.root_canon = std::filesystem::canonical(s.cfg.static_root, ec);
  }

  s.accept_next();
  s.schedule_pump();
  s.thread = std::thread([this] { impl_->ioc.run(); });
  s.command_thread = std::thread([this] { impl_->commands.run(); });
}

void Server::stop() {
  if (!impl_ || !impl_->thread.joinable()) return;
  impl_->commands_work.reset();
  impl_->commands.stop();  // a command already running finishes first
  impl_->command_thread.join();
  impl_->ioc.stop();
  impl_->thread.join();
  for (auto& s : impl_->sessions) s.reset();  // before ioc is destroyed
}

std::uint16_t Server::port() const { return impl_->acceptor.local_endpoint().port(); }

void Server::on_row(const SpectrumRow& row) noexcept {
  if (row.dbfs.empty() || row.dbfs.size() > kMaxBins) return;
  for (int i = 0; i < kMaxSockets; ++i) impl_->channels[i].publish(row);
}

void Server::on_info(const EngineInfo& info) {
  net::post(impl_->ioc, [this, info] { impl_->broadcast(detail::info_to_json(info, std::nullopt)); });
}

void Server::on_error(const ControlError& error) {
  net::post(impl_->ioc, [this, error] {
    impl_->broadcast(detail::error_json(std::nullopt, error.code, error.message));
  });
}

}  // namespace bl::api
