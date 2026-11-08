// WAV IQ reader (docs/api/testdata.md "WAV IQ"): golden vectors from tools/py/wav_iq.py,
// file-name centre frequency, rejects, looping, and a header fuzz loop.
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "io/iq_source.hpp"
#include "io/wav.hpp"

using namespace bl;

namespace {

const std::string kWavDir = std::string(BL_TESTDATA_DIR) + "/wav/";

std::string le16(std::uint16_t v) { return {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)}; }

std::string le32(std::uint32_t v) { return le16(static_cast<std::uint16_t>(v)) + le16(static_cast<std::uint16_t>(v >> 16)); }

std::string chunk(const std::string& id, const std::string& body) {
  return id + le32(static_cast<std::uint32_t>(body.size())) + body + (body.size() % 2 ? std::string(1, '\0') : "");
}

std::string fmt_body(std::uint16_t tag, std::uint16_t channels, std::uint32_t rate_sps, std::uint16_t bits) {
  const auto block_align = static_cast<std::uint16_t>(channels * bits / 8);
  return le16(tag) + le16(channels) + le32(rate_sps) + le32(rate_sps * block_align) + le16(block_align) + le16(bits);
}

std::string riff(const std::string& body) { return "RIFF" + le32(static_cast<std::uint32_t>(4 + body.size())) + "WAVE" + body; }

// 2-channel s16 file with `frames` zero frames.
std::string s16_wav(std::size_t frames) {
  return riff(chunk("fmt ", fmt_body(1, 2, 48000, 16)) + chunk("data", std::string(4 * frames, '\0')));
}

std::variant<WavLayout, IqOpenError> parse(const std::string& bytes) {
  std::istringstream in(bytes);
  return parse_wav_header(in);
}

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::filesystem::path write_temp(const char* name, const std::string& bytes) {
  const auto path = std::filesystem::temp_directory_path() / (std::string("bandlantern_wav_") + name);
  std::ofstream(path, std::ios::binary) << bytes;
  return path;
}

IIqSource& source(IqOpenResult& r) { return *std::get<std::unique_ptr<IIqSource>>(r); }

bool is_error(const IqOpenResult& r, const std::string& code) {
  const auto* error = std::get_if<IqOpenError>(&r);
  return error && error->code == code;
}

}  // namespace

TEST_CASE("WAV reader matches the Python golden vectors", "[io][wav][golden]") {
  const auto expected = nlohmann::json::parse(read_file(kWavDir + "iq_vectors.json"));
  REQUIRE(expected.size() == 6);
  for (const auto& [name, vector] : expected.items()) {
    INFO(name);
    auto opened = open_iq_file(kWavDir + name, false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    auto& reader = source(opened);
    CHECK(datatype_name(reader.meta().datatype) == vector["datatype"].get<std::string>());
    CHECK(reader.meta().sample_rate_sps == vector["rate_sps"].get<double>());
    CHECK(reader.meta().center_freq_hz == 0.0);

    std::vector<Cf32> out(64);
    const auto& samples = vector["samples"];
    REQUIRE(reader.read(out) == samples.size());
    for (std::size_t n = 0; n < samples.size(); ++n) {
      CHECK(std::abs(out[n].real() - samples[n][0].get<double>()) <= 1e-6);
      CHECK(std::abs(out[n].imag() - samples[n][1].get<double>()) <= 1e-6);
    }
  }
}

TEST_CASE("centre frequency comes from a <digits>Hz file name token", "[io][wav]") {
  CHECK(center_freq_from_filename("baseband_162475000Hz_13-04-51_26-10-2026.wav") == 162475000.0);
  CHECK(center_freq_from_filename("capture_20261026_130451Z_145800000Hz_IQ.wav") == 145800000.0);
  CHECK(center_freq_from_filename("7100000Hz.wav") == 7100000.0);
  CHECK(center_freq_from_filename("a_1Hz_2Hz.wav") == 1.0);  // first token wins
  CHECK_FALSE(center_freq_from_filename("capture.wav").has_value());
  CHECK_FALSE(center_freq_from_filename("Hz_.wav").has_value());
  CHECK_FALSE(center_freq_from_filename("").has_value());
  CHECK_FALSE(center_freq_from_filename("x_145.8MHz_IQ.wav").has_value());
  CHECK_FALSE(center_freq_from_filename("x_145800000hz.wav").has_value());
  CHECK_FALSE(center_freq_from_filename("x_-100Hz.wav").has_value());
  CHECK_FALSE(center_freq_from_filename("x_99999999999999999999999Hz.wav").has_value());
}

TEST_CASE("WAV centre frequency reaches meta() through open_iq_file", "[io][wav]") {
  const auto path = std::filesystem::temp_directory_path() / "bandlantern_wav_dir";
  std::filesystem::create_directories(path);
  const auto file = path / "baseband_7100000Hz_x.wav";
  std::ofstream(file, std::ios::binary) << s16_wav(4);
  {
    // Scoped so the reader closes the file before remove_all (Windows refuses to delete open files).
    auto opened = open_iq_file(file, false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    CHECK(source(opened).meta().center_freq_hz == 7100000.0);
    CHECK(datatype_name(source(opened).meta().datatype) == "cs16_le");
  }
  std::filesystem::remove_all(path);
}

TEST_CASE("WAV header rejects what is not 2-channel u8/s16/f32 IQ", "[io][wav]") {
  const auto rejects = [](const std::string& bytes) {
    auto r = parse(bytes);
    REQUIRE(std::holds_alternative<IqOpenError>(r));
    CHECK(std::get<IqOpenError>(r).code == "bad_format");
    return std::get<IqOpenError>(r).message;
  };
  const std::string data = chunk("data", std::string(8, '\0'));
  CHECK(rejects(riff(chunk("fmt ", fmt_body(1, 1, 48000, 16)) + data)).find("2-channel") != std::string::npos);
  rejects(riff(chunk("fmt ", fmt_body(1, 2, 48000, 24)) + data));                // 24-bit PCM
  rejects(riff(chunk("fmt ", fmt_body(3, 2, 48000, 64)) + data));                // f64
  rejects(riff(chunk("fmt ", fmt_body(7, 2, 8000, 8)) + data));                  // mu-law
  rejects(riff(chunk("fmt ", fmt_body(1, 2, 0, 16)) + data));                    // rate 0
  rejects(riff(chunk("fmt ", fmt_body(1, 2, 48000, 16).substr(0, 14)) + data));  // fmt too short
  rejects(riff(chunk("fmt ", "") + data));                                       // empty fmt
  std::string bad_align = fmt_body(1, 2, 48000, 16);
  bad_align[12] = 3;
  rejects(riff(chunk("fmt ", bad_align) + data));
  // EXTENSIBLE with a SubFormat GUID that is not KSDATAFORMAT_SUBTYPE_*.
  rejects(riff(chunk("fmt ", fmt_body(0xFFFE, 2, 48000, 16) + le16(22) + le16(16) + le32(3) + std::string(16, 'x')) + data));
  rejects(riff(chunk("fmt ", fmt_body(0xFFFE, 2, 48000, 16))) + data);  // EXTENSIBLE, no extension
  rejects(riff(data));                                                  // no fmt
  rejects(riff(chunk("fmt ", fmt_body(1, 2, 48000, 16))));              // no data
  rejects("fmt " + le32(0xFFFFFFFF));                                   // not RIFF at all
  rejects("RIFX" + s16_wav(1).substr(4));
  rejects("RF64" + s16_wav(1).substr(4));
  rejects(s16_wav(1).substr(0, 20));  // cut inside the fmt chunk header
  rejects(s16_wav(1).substr(0, 30));  // cut inside the fmt body
  rejects("RIFF");
  rejects("");
}

TEST_CASE("WAV data size is clamped to the file and to whole frames", "[io][wav]") {
  const std::string fmt = chunk("fmt ", fmt_body(1, 2, 48000, 16));
  SECTION("declared size past the end of the file (recording cut short)") {
    auto r = parse(riff(fmt + "data" + le32(0xFFFFFFFF) + std::string(10, '\0')));
    REQUIRE(std::holds_alternative<WavLayout>(r));
    CHECK(std::get<WavLayout>(r).data_size_bytes == 8);  // 10 bytes on disk -> 2 whole frames
  }
  SECTION("data header is the last thing in the file") {
    auto r = parse(riff(fmt + "data" + le32(100)));
    REQUIRE(std::holds_alternative<WavLayout>(r));
    CHECK(std::get<WavLayout>(r).data_size_bytes == 0);
  }
  SECTION("trailing chunk after data is not sample data") {
    auto r = parse(riff(fmt + chunk("data", std::string(8, '\0')) + chunk("LIST", "abc")));
    REQUIRE(std::holds_alternative<WavLayout>(r));
    CHECK(std::get<WavLayout>(r).data_size_bytes == 8);
    CHECK(std::get<WavLayout>(r).data_offset_bytes == 12 + 24 + 8);
  }
}

TEST_CASE("WAV reader loops, stops at EOF, and reports missing files", "[io][wav]") {
  const auto path = write_temp("loop.wav", riff(chunk("fmt ", fmt_body(1, 2, 48000, 16)) +
                                                chunk("data", le16(16384) + le16(0xC000) + std::string(12, '\0'))));
  SECTION("looping wraps to the first frame") {
    auto opened = open_iq_file(path, true);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    std::vector<Cf32> out(10);
    REQUIRE(source(opened).read(out) == 10);
    CHECK(out[0] == Cf32(0.5f, -0.5f));
    CHECK(out[4] == Cf32(0.5f, -0.5f));
    CHECK(out[1] == Cf32(0.0f, 0.0f));
  }
  SECTION("non-looping ends") {
    auto opened = open_iq_file(path, false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    std::vector<Cf32> out(10);
    CHECK(source(opened).read(out) == 4);
    CHECK(source(opened).read(out) == 0);
    CHECK_FALSE(source(opened).failed());
  }
  SECTION("empty data chunk while looping returns 0 instead of spinning") {
    const auto empty = write_temp("empty.wav", s16_wav(0));
    auto opened = open_iq_file(empty, true);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    std::vector<Cf32> out(4);
    CHECK(source(opened).read(out) == 0);
  }
  SECTION("odd byte count: the torn last frame is dropped") {
    const auto torn = write_temp("torn.wav", s16_wav(3) + "xyz");
    auto opened = open_iq_file(torn, false);
    REQUIRE(std::holds_alternative<std::unique_ptr<IIqSource>>(opened));
    std::vector<Cf32> out(8);
    CHECK(source(opened).read(out) == 3);
  }
  SECTION("zero-length and directory paths") {
    CHECK(is_error(open_iq_file(write_temp("zero.wav", ""), false), "bad_format"));
    const auto dir = std::filesystem::temp_directory_path() / "bandlantern_wav_isdir.wav";
    std::filesystem::create_directories(dir);
    CHECK(is_error(open_iq_file(dir, false), "bad_format"));
    std::filesystem::remove_all(dir);
  }
  CHECK(is_error(open_iq_file(std::filesystem::temp_directory_path() / "bandlantern_wav_missing.wav", false), "not_found"));
}

// Fuzz: mutated and truncated headers must never crash (run under macos-asan) and an accepted
// layout must stay inside the input.
TEST_CASE("WAV header parser survives mutated input", "[io][wav][fuzz]") {
  const std::vector<std::string> seeds = {read_file(kWavDir + "iq_s16_chunks.wav"),
                                          read_file(kWavDir + "iq_f32_extensible.wav"),
                                          read_file(kWavDir + "iq_u8.wav")};
  std::mt19937 rng(1);
  for (int iteration = 0; iteration < 20000; ++iteration) {
    std::string bytes = seeds[iteration % seeds.size()];
    const int mutations = 1 + static_cast<int>(rng() % 8);
    for (int m = 0; m < mutations; ++m) {
      const std::size_t at = rng() % std::min<std::size_t>(bytes.size(), 80);
      bytes[at] = static_cast<char>(rng() % 4 == 0 ? 0xFF : rng());
    }
    if (rng() % 4 == 0) bytes.resize(rng() % bytes.size());

    auto r = parse(bytes);
    const auto* layout = std::get_if<WavLayout>(&r);
    if (!layout) continue;
    CHECK(layout->data_offset_bytes + layout->data_size_bytes <= bytes.size());
    CHECK(layout->data_size_bytes % layout->block_align_bytes == 0);
  }
}
