#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "io/iq_source.hpp"
#include "io/sigmf.hpp"

using namespace bl;

namespace {

// The example from docs/api/testdata.md.
const char* const kExampleMeta = R"({
  "global": {
    "core:datatype": "cu8",
    "core:sample_rate": 2400000,
    "core:version": "1.2.0",
    "core:description": "Three tones + noise, seed 1",
    "core:recorder": "bandlantern synth_iq.py"
  },
  "captures": [ { "core:sample_start": 0, "core:frequency": 100000000 } ],
  "annotations": [ {
      "core:sample_start": 0, "core:freq_lower_edge": 100149500,
      "core:freq_upper_edge": 100150500, "core:label": "tone -20 dBFS" } ]
})";

std::filesystem::path make_temp_dir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / (std::string("bandlantern_") + name);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void write_file(const std::filesystem::path& path, const void* data, std::size_t bytes) {
  std::ofstream f(path, std::ios::binary);
  f.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  write_file(path, text.data(), text.size());
}

std::string meta_with(const std::string& datatype) {
  return R"({"global":{"core:datatype":")" + datatype + R"(","core:sample_rate":48000},
    "captures":[{"core:sample_start":0,"core:frequency":7100000}]})";
}

bool is_error(const IqOpenResult& r, const std::string& code) {
  const auto* error = std::get_if<IqOpenError>(&r);
  return error && error->code == code;
}

IIqSource& source(IqOpenResult& r) { return *std::get<std::unique_ptr<IIqSource>>(r); }

}  // namespace

TEST_CASE("meta parser accepts the testdata.md example", "[io][sigmf]") {
  auto r = parse_sigmf_meta(kExampleMeta);
  REQUIRE(std::holds_alternative<IqMeta>(r));
  const auto& meta = std::get<IqMeta>(r);
  CHECK(meta.datatype == IqDatatype::cu8);
  CHECK(meta.sample_rate_sps == 2400000.0);
  CHECK(meta.center_freq_hz == 100000000.0);
}

TEST_CASE("meta parser rejects each missing or wrong field with bad_format", "[io][sigmf]") {
  using nlohmann::json;
  const auto reject = [](const json& doc) {
    auto r = parse_sigmf_meta(doc.dump());
    REQUIRE(std::holds_alternative<IqOpenError>(r));
    CHECK(std::get<IqOpenError>(r).code == "bad_format");
  };
  const json base = json::parse(kExampleMeta);

  json j = base; j["global"].erase("core:datatype"); reject(j);
  j = base; j["global"].erase("core:sample_rate"); reject(j);
  j = base; j["captures"][0].erase("core:frequency"); reject(j);
  j = base; j["captures"] = json::array(); reject(j);
  j = base; j["captures"] = json::array({5}); reject(j);
  j = base; j.erase("captures"); reject(j);
  j = base; j.erase("global"); reject(j);
  j = base; j["global"] = 3; reject(j);
  j = base; j["global"]["core:datatype"] = "ci16_le"; reject(j);
  j = base; j["global"]["core:datatype"] = "cs16_le"; reject(j);  // WAV only
  j = base; j["global"]["core:datatype"] = 8; reject(j);
  j = base; j["global"]["core:sample_rate"] = "fast"; reject(j);
  j = base; j["global"]["core:sample_rate"] = 0; reject(j);
  j = base; j["global"]["core:sample_rate"] = -5; reject(j);
  j = base; j["captures"][0]["core:frequency"] = "100M"; reject(j);

  for (const char* text : {"not json {", "", "[]", "null", "{", "\xff\xfe", R"({"global":{"core:sample_rate":1e999}})"}) {
    auto r = parse_sigmf_meta(text);
    REQUIRE(std::holds_alternative<IqOpenError>(r));
    CHECK(std::get<IqOpenError>(r).code == "bad_format");
  }
}

TEST_CASE("meta parser uses the first capture's frequency", "[io][sigmf]") {
  auto r = parse_sigmf_meta(R"({"global":{"core:datatype":"cf32_le","core:sample_rate":1000},
    "captures":[{"core:sample_start":0,"core:frequency":1e6},{"core:sample_start":500,"core:frequency":2e6}]})");
  REQUIRE(std::holds_alternative<IqMeta>(r));
  CHECK(std::get<IqMeta>(r).center_freq_hz == 1e6);
}

TEST_CASE("cu8 conversion of 0/127/128/255", "[io][sigmf]") {
  const std::uint8_t bytes[] = {0, 127, 128, 255};
  Cf32 out[2];
  convert_cu8_to_cf32(bytes, out);
  CHECK(out[0].real() == -1.0f);
  CHECK(out[0].imag() == (127.0f - 127.5f) / 127.5f);
  CHECK(out[1].real() == (128.0f - 127.5f) / 127.5f);
  CHECK(out[1].imag() == 1.0f);
}

TEST_CASE("SigMF round trip cu8 and cf32_le, with and without loop", "[io][sigmf]") {
  const auto dir = make_temp_dir("sigmf_roundtrip");

  SECTION("cu8") {
    write_text(dir / "a.sigmf-meta", meta_with("cu8"));
    const std::uint8_t data[] = {0, 255, 127, 128, 255, 0, 1};  // 3 samples + torn byte
    write_file(dir / "a.sigmf-data", data, sizeof data);

    auto opened = open_iq_file(dir / "a.sigmf-meta", false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    CHECK(source(opened).meta().center_freq_hz == 7100000.0);
    CHECK(source(opened).meta().sample_rate_sps == 48000.0);
    Cf32 out[8];
    CHECK(source(opened).read(out) == 3);
    CHECK(out[0] == Cf32(-1.0f, 1.0f));
    CHECK(out[2] == Cf32(1.0f, -1.0f));
    CHECK(source(opened).read(out) == 0);
    CHECK_FALSE(source(opened).failed());

    auto looped = open_iq_file(dir / "a.sigmf-meta", true);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(looped));
    Cf32 more[7];
    CHECK(source(looped).read(more) == 7);  // wraps: 3 + 3 + 1
    CHECK(more[3] == more[0]);
    CHECK(more[6] == more[0]);
  }

  SECTION("cf32_le") {
    write_text(dir / "b.sigmf-meta", meta_with("cf32_le"));
    const float data[] = {0.5f, -0.25f, 1.0f, 0.0f};
    write_file(dir / "b.sigmf-data", data, sizeof data);

    auto opened = open_iq_file(dir / "b.sigmf-meta", false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    Cf32 out[4];
    CHECK(source(opened).read(out) == 2);
    CHECK(out[0] == Cf32(0.5f, -0.25f));
    CHECK(out[1] == Cf32(1.0f, 0.0f));
  }

  std::filesystem::remove_all(dir);
}

TEST_CASE("SigMF files that are missing, empty or damaged give not_found or bad_format", "[io][sigmf]") {
  const auto dir = make_temp_dir("sigmf_errors");

  SECTION("meta missing") { CHECK(is_error(open_iq_file(dir / "nope.sigmf-meta", false), "not_found")); }

  SECTION("data missing") {
    write_text(dir / "a.sigmf-meta", meta_with("cu8"));
    CHECK(is_error(open_iq_file(dir / "a.sigmf-meta", false), "not_found"));
  }

  SECTION("meta lacks fields") {
    write_text(dir / "c.sigmf-meta", R"({"global":{"core:sample_rate":1}})");
    write_text(dir / "c.sigmf-data", "xx");
    CHECK(is_error(open_iq_file(dir / "c.sigmf-meta", false), "bad_format"));
  }

  SECTION("meta is empty, binary or truncated") {
    write_text(dir / "d.sigmf-data", "xx");
    write_text(dir / "d.sigmf-meta", "");
    CHECK(is_error(open_iq_file(dir / "d.sigmf-meta", false), "bad_format"));
    write_text(dir / "d.sigmf-meta", std::string("\x00\x01\x02\xff", 4));
    CHECK(is_error(open_iq_file(dir / "d.sigmf-meta", false), "bad_format"));
    write_text(dir / "d.sigmf-meta", meta_with("cu8").substr(0, 40));
    CHECK(is_error(open_iq_file(dir / "d.sigmf-meta", false), "bad_format"));
  }

  SECTION("meta is a directory") {
    std::filesystem::create_directories(dir / "e.sigmf-meta");
    CHECK(is_error(open_iq_file(dir / "e.sigmf-meta", false), "bad_format"));
  }

  SECTION("data is a directory") {
    write_text(dir / "f.sigmf-meta", meta_with("cu8"));
    std::filesystem::create_directories(dir / "f.sigmf-data");
    CHECK(is_error(open_iq_file(dir / "f.sigmf-meta", false), "bad_format"));
  }

  SECTION("zero-length data opens and reads 0, also while looping") {
    write_text(dir / "g.sigmf-meta", meta_with("cf32_le"));
    write_text(dir / "g.sigmf-data", "");
    for (const bool loop : {false, true}) {
      auto opened = open_iq_file(dir / "g.sigmf-meta", loop);
      REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
      Cf32 out[4];
      CHECK(source(opened).read(out) == 0);
      CHECK_FALSE(source(opened).failed());
    }
  }

  SECTION("data shorter than one sample (odd byte count) reads 0") {
    write_text(dir / "h.sigmf-meta", meta_with("cf32_le"));
    write_text(dir / "h.sigmf-data", "1234567");  // 7 of 8 bytes
    auto opened = open_iq_file(dir / "h.sigmf-meta", true);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    Cf32 out[4];
    CHECK(source(opened).read(out) == 0);
  }

  SECTION("unsupported extension") {
    write_text(dir / "i.txt", "x");
    CHECK(is_error(open_iq_file(dir / "i.txt", false), "bad_format"));
    CHECK(is_error(open_iq_file(dir / "noext", false), "bad_format"));
  }

  std::filesystem::remove_all(dir);
}
