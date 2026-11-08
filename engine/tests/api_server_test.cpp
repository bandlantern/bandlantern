// Real sockets on 127.0.0.1 (ephemeral port) with a Beast client.
#include <catch2/catch_test_macros.hpp>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "api/server.hpp"
#include "api/token.hpp"

using namespace bl::api;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace {

// Touched only on the server's command thread, one call at a time.
class FakeController : public IController {
 public:
  ControlResult get_info() override { return info_; }
  ControlResult play(const std::string& path, bool loop) override {
    if (path == "slow.sigmf-meta") std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (path == "missing.sigmf-meta") return ControlError{"not_found", "no such file"};
    info_.streaming = true;
    info_.source = SourceInfo{"sigmf", path, "cu8", 2400000.0, 100e6, loop};
    return info_;
  }
  ControlResult stop() override {
    info_.streaming = false;
    info_.source.reset();
    return info_;
  }
  ControlResult set_fft(std::optional<std::uint32_t> size, std::optional<double> rows_per_s) override {
    if (size) info_.fft_size = *size;
    if (rows_per_s) info_.rows_per_s = *rows_per_s;
    return info_;
  }

 private:
  EngineInfo info_;
};

struct TestServer {
  explicit TestServer(ServerConfig c = {}) : cfg(std::move(c)) {
    if (cfg.token.empty()) cfg.token = generate_token();
    cfg.port = 0;
    server = std::make_unique<Server>(cfg);
    server->start(ctl);
  }
  std::string host() const { return "127.0.0.1:" + std::to_string(server->port()); }
  std::string own_origin() const { return "http://" + host(); }

  FakeController ctl;
  ServerConfig cfg;
  std::unique_ptr<Server> server;
};

using Ws = websocket::stream<tcp::socket>;

struct Client {
  net::io_context ioc;
  Ws ws{ioc};

  // Throws boost::system::system_error if the upgrade is refused.
  void connect(const TestServer& s, const std::string& host, const std::string& origin) {
    ws.next_layer().connect({net::ip::make_address_v4("127.0.0.1"), s.server->port()});
    if (!origin.empty()) {
      ws.set_option(websocket::stream_base::decorator(
          [origin](websocket::request_type& r) { r.set(http::field::origin, origin); }));
    }
    ws.handshake(host, "/ws");
  }

  void connect_ok(const TestServer& s) { connect(s, s.host(), s.own_origin()); }

  void send(const json& j) {
    ws.text(true);
    ws.write(net::buffer(j.dump()));
  }

  void hello(const std::string& token, int id = 1) { send({{"v", 1}, {"type", "hello"}, {"id", id}, {"token", token}}); }

  struct Msg {
    bool binary;
    std::string data;
  };
  Msg read() {
    beast::flat_buffer b;
    ws.read(b);
    return {ws.got_binary(), beast::buffers_to_string(b.data())};
  }
  json read_json() {
    const Msg m = read();
    REQUIRE_FALSE(m.binary);
    return json::parse(m.data);
  }

  // Reads until the server's close; returns its close reason.
  websocket::close_reason read_until_close() {
    beast::flat_buffer b;
    beast::error_code ec;
    for (;;) {
      ws.read(b, ec);
      if (ec) break;
      b.clear();
    }
    REQUIRE(ec == websocket::error::closed);
    return ws.reason();
  }
};

std::uint64_t frame_seq(const std::string& f) {
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | std::uint8_t(f[16 + i]);
  return v;
}

bl::SpectrumRow make_row(const std::vector<float>& bins, std::uint64_t sample_index) {
  bl::SpectrumRow r;
  r.sample_index = sample_index;
  r.center_freq_hz = 100e6;
  r.rate_sps = 2.4e6;
  r.dbfs = bins;
  return r;
}

http::response<http::string_body> http_get(const TestServer& s, const std::string& target, const std::string& host,
                                           const std::string& origin = "") {
  net::io_context ioc;
  tcp::socket sock(ioc);
  sock.connect({net::ip::make_address_v4("127.0.0.1"), s.server->port()});
  http::request<http::empty_body> req{http::verb::get, target, 11};
  req.set(http::field::host, host);
  if (!origin.empty()) req.set(http::field::origin, origin);
  http::write(sock, req);
  beast::flat_buffer b;
  http::response<http::string_body> res;
  http::read(sock, b, res);
  return res;
}

bool upgrade_refused(const TestServer& s, const std::string& host, const std::string& origin) {
  Client c;
  try {
    c.connect(s, host, origin);
  } catch (const boost::system::system_error&) {
    return true;
  }
  return false;
}

// Authenticated client: connect, hello, consume hello_ok.
void login(Client& c, const TestServer& s) {
  c.connect_ok(s);
  c.hello(s.cfg.token);
  const json ok = c.read_json();
  REQUIRE(ok["type"] == "hello_ok");
  REQUIRE(ok["re"] == 1);
}

json read_json_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  REQUIRE(file);
  return json::parse(file);
}

json schema_examples(const std::string& name) { return read_json_file(std::filesystem::path(BL_TESTDATA_DIR) / "api" / "messages" / name); }

// The example of `type` replying to command `re` (events.json).
json find_example(const json& events, const std::string& type, int re) {
  for (const json& e : events)
    if (e["type"] == type && e.value("re", 0) == re) return e;
  FAIL("no " << type << " example with re " << re);
  return {};
}

void check_same_keys(const json& actual, const json& example) {
  std::set<std::string> a, e;
  for (const auto& [k, _] : actual.items()) a.insert(k);
  for (const auto& [k, _] : example.items()) e.insert(k);
  INFO(actual.dump());
  CHECK(a == e);
}

}  // namespace

TEST_CASE("Host header must be loopback with the engine port", "[api][server]") {
  TestServer s;
  CHECK(http_get(s, "/", "evil.example").result() == http::status::forbidden);
  CHECK(http_get(s, "/", "evil.example:" + std::to_string(s.server->port())).result() == http::status::forbidden);
  CHECK(upgrade_refused(s, "evil.example", s.own_origin()));
  Client c;
  CHECK_NOTHROW(c.connect(s, "LOCALHOST:" + std::to_string(s.server->port()),
                          "http://localhost:" + std::to_string(s.server->port())));
}

TEST_CASE("Origin checks on the WebSocket upgrade", "[api][server]") {
  TestServer s;
  CHECK(upgrade_refused(s, s.host(), "http://evil.example"));
  CHECK(upgrade_refused(s, s.host(), "http://localhost:5173"));  // dev origin not configured
  CHECK(upgrade_refused(s, s.host(), ""));                       // missing Origin
  CHECK_FALSE(upgrade_refused(s, s.host(), s.own_origin()));

  ServerConfig cfg;
  cfg.dev_origins = {"http://localhost:5173"};
  TestServer dev(cfg);
  CHECK_FALSE(upgrade_refused(dev, dev.host(), "http://localhost:5173"));
  CHECK(upgrade_refused(dev, dev.host(), "http://localhost:5174"));
}

TEST_CASE("bad Origin on plain HTTP gets 403", "[api][server]") {
  TestServer s;
  CHECK(http_get(s, "/", s.host(), "http://evil.example").result() == http::status::forbidden);
}

TEST_CASE("wrong token closes with 1008", "[api][server]") {
  TestServer s;
  Client c;
  c.connect_ok(s);
  c.hello(generate_token());
  const auto r = c.read_until_close();
  CHECK(r.code == websocket::close_code::policy_error);
  CHECK(std::string(r.reason.c_str()) == "auth");
}

TEST_CASE("first message other than hello closes with 1008", "[api][server]") {
  TestServer s;
  Client c;
  c.connect_ok(s);
  c.send({{"v", 1}, {"type", "get_info"}, {"id", 1}});
  CHECK(c.read_until_close().code == websocket::close_code::policy_error);
}

TEST_CASE("no hello within the deadline closes with 1008", "[api][server]") {
  ServerConfig cfg;
  cfg.auth_timeout_ms = 150;
  TestServer s(cfg);
  Client c;
  c.connect_ok(s);
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = c.read_until_close();
  CHECK(r.code == websocket::close_code::policy_error);
  CHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(100));
}

TEST_CASE("a ninth authenticated socket is closed with 1011 busy", "[api][server]") {
  TestServer s;
  std::vector<std::unique_ptr<Client>> clients;
  for (int i = 0; i < Server::kMaxSockets; ++i) {
    clients.push_back(std::make_unique<Client>());
    login(*clients.back(), s);
  }
  Client extra;
  extra.connect_ok(s);
  extra.hello(s.cfg.token);
  const auto r = extra.read_until_close();
  CHECK(r.code == websocket::close_code::internal_error);
  CHECK(std::string(r.reason.c_str()) == "busy");
}

TEST_CASE("published row arrives after hello_ok with seq 0", "[api][server]") {
  TestServer s;
  Client c;
  login(c, s);
  const std::vector<float> bins(2048, -80.0f);
  s.server->on_row(make_row(bins, 12345));
  const auto m = c.read();
  REQUIRE(m.binary);
  REQUIRE(m.data.size() == 48 + 2048 * 4);
  CHECK(m.data.substr(0, 4) == std::string("BLF\0", 4));
  CHECK(frame_seq(m.data) == 0);
}

TEST_CASE("a slow command does not hold back spectrum frames; replies keep order", "[api][server]") {
  TestServer s;
  Client c;
  login(c, s);
  c.send({{"v", 1}, {"type", "play"}, {"id", 2}, {"path", "slow.sigmf-meta"}});
  c.send({{"v", 1}, {"type", "nope"}, {"id", 3}});  // instant error, must still come second
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // the controller is now busy
  s.server->on_row(make_row(std::vector<float>(2048, -80.0f), 0));

  const auto first = c.read();
  CHECK(first.binary);  // the row, while play is still running
  const json reply = json::parse(c.read().data);
  CHECK(reply["re"] == 2);
  CHECK(reply["type"] == "info");
  const json error = json::parse(c.read().data);
  CHECK(error["re"] == 3);
  CHECK(error["code"] == "unknown_type");
}

TEST_CASE("silent client is dropped after the idle timeout", "[api][server]") {
  ServerConfig cfg;
  cfg.ws_idle_timeout_ms = 200;
  TestServer s(cfg);
  Client c;
  login(c, s);
  // Not reading means the server's ping goes unanswered.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  beast::flat_buffer b;
  beast::error_code ec;
  c.ws.read(b, ec);
  CHECK(ec);
}

TEST_CASE("client answering pings survives past the idle timeout", "[api][server]") {
  ServerConfig cfg;
  cfg.ws_idle_timeout_ms = 200;
  TestServer s(cfg);
  Client c;
  login(c, s);
  // A blocking read answers pings; the row arrives well after the idle timeout.
  std::thread publisher([&s] {
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    s.server->on_row(make_row(std::vector<float>(2048, -80.0f), 0));
  });
  const auto m = c.read();
  publisher.join();
  CHECK(m.binary);
}

TEST_CASE("binary or oversize or non-JSON from client closes with 1002", "[api][server]") {
  TestServer s;
  {
    Client c;
    login(c, s);
    c.ws.binary(true);
    c.ws.write(net::buffer(std::string(4, 'x')));
    CHECK(c.read_until_close().code == websocket::close_code::protocol_error);
  }
  {
    Client c;
    login(c, s);
    c.ws.text(true);
    c.ws.write(net::buffer(std::string(70000, 'a')));
    CHECK(c.read_until_close().code == websocket::close_code::protocol_error);
  }
  {
    Client c;
    login(c, s);
    c.ws.text(true);
    c.ws.write(net::buffer(std::string("not json")));
    CHECK(c.read_until_close().code == websocket::close_code::protocol_error);
  }
}

TEST_CASE("slow client: 8 rows delivered, rest dropped and reported", "[api][server]") {
  ServerConfig cfg;
  cfg.pump_interval_ms = 150;  // the burst below must finish before the first pump
  cfg.drop_event_interval_ms = 50;
  TestServer s(cfg);
  Client c;
  login(c, s);

  const std::vector<float> bins(256, -80.0f);
  for (int i = 0; i < 20; ++i) s.server->on_row(make_row(bins, i));

  std::vector<std::uint64_t> seqs;
  std::uint64_t dropped = 0;
  while (seqs.size() < 8 || dropped < 12) {
    const auto m = c.read();
    if (m.binary) {
      seqs.push_back(frame_seq(m.data));
    } else {
      const json j = json::parse(m.data);
      REQUIRE(j["type"] == "drop");
      dropped += j["rows_dropped"].get<std::uint64_t>();
    }
  }
  CHECK(seqs == std::vector<std::uint64_t>{0, 1, 2, 3, 4, 5, 6, 7});
  CHECK(dropped == 12);

  // seq kept counting through the dropped rows, so the next row shows the gap.
  s.server->on_row(make_row(bins, 99));
  const auto m = c.read();
  REQUIRE(m.binary);
  CHECK(frame_seq(m.data) == 20);
}

TEST_CASE("commands are dispatched to the controller and checked first", "[api][server]") {
  TestServer s;
  Client c;
  login(c, s);

  c.send({{"v", 1}, {"type", "set_fft"}, {"id", 5}, {"size", 4096}, {"rows_per_s", 20}});
  json r = c.read_json();
  CHECK(r["type"] == "info");
  CHECK(r["re"] == 5);
  CHECK(r["fft_size"] == 4096);
  CHECK(r["source"].is_null());

  c.send({{"v", 1}, {"type", "set_fft"}, {"id", 6}, {"size", 1000}});
  r = c.read_json();
  CHECK(r["code"] == "bad_value");
  CHECK(r["re"] == 6);
  c.send({{"v", 1}, {"type", "set_fft"}, {"id", 7}, {"rows_per_s", 61}});
  CHECK(c.read_json()["code"] == "bad_value");
  c.send({{"v", 1}, {"type", "set_fft"}, {"id", 8}, {"size", 2048.0}});
  CHECK(c.read_json()["code"] == "bad_request");

  c.send({{"v", 1}, {"type", "play"}, {"id", 9}, {"path", "a.sigmf-meta"}, {"loop", true}});
  r = c.read_json();
  CHECK(r["streaming"] == true);
  CHECK(r["source"]["loop"] == true);
  CHECK(r["source"]["rate_sps"] == 2400000.0);

  c.send({{"v", 1}, {"type", "play"}, {"id", 10}, {"path", "missing.sigmf-meta"}});
  CHECK(c.read_json()["code"] == "not_found");
  c.send({{"v", 1}, {"type", "play"}, {"id", 11}, {"path", 7}});
  CHECK(c.read_json()["code"] == "bad_request");

  c.send({{"v", 1}, {"type", "stop"}, {"id", 12}});
  CHECK(c.read_json()["streaming"] == false);

  c.send({{"v", 1}, {"type", "get_info"}, {"id", 13}});
  CHECK(c.read_json()["type"] == "info");

  c.send({{"v", 1}, {"type", "nope"}, {"id", 14}});
  CHECK(c.read_json()["code"] == "unknown_type");

  c.send({{"v", 2}, {"type", "get_info"}, {"id", 15}});
  CHECK(c.read_json()["code"] == "bad_version");

  c.send({{"v", 1}, {"type", "get_info"}});
  const json e = c.read_json();
  CHECK(e["code"] == "bad_request");
  CHECK_FALSE(e.contains("re"));
}

TEST_CASE("on_info and on_error reach every authenticated socket without re", "[api][server]") {
  TestServer s;
  Client a, b;
  login(a, s);
  login(b, s);

  s.server->on_info(EngineInfo{});
  s.server->on_error(ControlError{"source_error", "read failed"});
  for (Client* c : {&a, &b}) {
    const json info = c->read_json();
    CHECK(info["type"] == "info");
    CHECK_FALSE(info.contains("re"));
    const json error = c->read_json();
    CHECK(error["code"] == "source_error");
    CHECK_FALSE(error.contains("re"));
  }
}

TEST_CASE("static files: served, traversal rejected, no CORS headers", "[api][server]") {
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() / ("bl_static_" + generate_token().substr(0, 8));
  fs::create_directories(base / "dist");
  std::ofstream(base / "dist" / "index.html") << "<h1>hi</h1>";
  std::ofstream(base / "secret.txt") << "secret";

  ServerConfig cfg;
  cfg.static_root = base / "dist";
  TestServer s(cfg);

  auto res = http_get(s, "/", s.host());
  CHECK(res.result() == http::status::ok);
  CHECK(res.body() == "<h1>hi</h1>");
  CHECK(res.find(http::field::access_control_allow_origin) == res.end());
  const std::string csp(res[http::field::content_security_policy]);
  CHECK(csp.find("script-src 'self'") != std::string::npos);
  CHECK(csp.find("connect-src 'self'") != std::string::npos);
  CHECK(csp.find("frame-ancestors 'none'") != std::string::npos);
  CHECK(res[http::field::referrer_policy] == "no-referrer");

  res = http_get(s, "/index.html", s.host(), s.own_origin());
  CHECK(res.result() == http::status::ok);
  CHECK(res.find(http::field::access_control_allow_origin) == res.end());

  for (const char* bad : {"/../secret.txt", "/%2e%2e/secret.txt", "/a/../../secret.txt", "/..\\secret.txt"}) {
    res = http_get(s, bad, s.host());
    CHECK(res.result() == http::status::forbidden);
    CHECK(res.body().empty());
    CHECK(res.find(http::field::access_control_allow_origin) == res.end());
  }
  CHECK(http_get(s, "/nope.html", s.host()).result() == http::status::not_found);

  fs::remove_all(base);
}

TEST_CASE("upgrade response carries no CORS header", "[api][server]") {
  TestServer s;
  Client c;
  websocket::response_type res;
  c.ws.next_layer().connect({net::ip::make_address_v4("127.0.0.1"), s.server->port()});
  c.ws.set_option(websocket::stream_base::decorator(
      [&](websocket::request_type& r) { r.set(http::field::origin, s.own_origin()); }));
  c.ws.handshake(res, s.host(), "/ws");
  CHECK(res.find(http::field::access_control_allow_origin) == res.end());
}

TEST_CASE("schema: every command example gets its reply", "[api][server][schema]") {
  TestServer s;
  Client c;
  login(c, s);

  for (const json& cmd : schema_examples("commands.json")) {
    if (cmd["type"] == "hello") continue;  // only valid as the first message; login() sent one
    INFO(cmd.dump());
    c.send(cmd);
    const json r = c.read_json();
    CHECK(r["re"] == cmd["id"]);
    CHECK(r["type"] == "info");  // the fake controller accepts every example
  }
}

TEST_CASE("schema: engine replies have the example messages' fields", "[api][server][schema]") {
  const json schema = read_json_file(BL_API_SCHEMA);
  const json events = schema_examples("events.json");
  TestServer s;
  Client c;
  c.connect_ok(s);
  c.hello(s.cfg.token);
  const json ok = c.read_json();
  check_same_keys(ok, find_example(events, "hello_ok", 1));
  CHECK(ok["api_version"] == schema["x-api-version"]);

  c.send({{"v", 1}, {"type", "play"}, {"id", 3}, {"path", "a.sigmf-meta"}, {"loop", true}});
  const json playing = c.read_json();
  check_same_keys(playing, find_example(events, "info", 3));
  check_same_keys(playing["source"], find_example(events, "info", 3)["source"]);

  c.send({{"v", 1}, {"type", "stop"}, {"id", 4}});
  check_same_keys(c.read_json(), find_example(events, "info", 4));

  c.send({{"v", 1}, {"type", "play"}, {"id", 3}, {"path", "missing.sigmf-meta"}});
  check_same_keys(c.read_json(), find_example(events, "error", 3));
}
