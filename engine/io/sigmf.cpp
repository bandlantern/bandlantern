#include "io/sigmf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <system_error>

namespace bl {

static_assert(std::endian::native == std::endian::little, "cf32_le is copied verbatim");

namespace {

constexpr std::size_t kScratchSamples = 16384;
constexpr std::uintmax_t kMaxMetaBytes = 16u << 20;  // a real meta file is a few KB

IqOpenError bad_format(std::string message) { return IqOpenError{"bad_format", std::move(message)}; }

std::size_t bytes_per_sample(IqDatatype type) { return type == IqDatatype::cu8 ? 2 : 8; }

bool is_finite_number(const nlohmann::json& value) {
  return value.is_number() && std::isfinite(value.get<double>());
}

}  // namespace

std::variant<IqMeta, IqOpenError> parse_sigmf_meta(std::string_view meta_json_text) {
  using nlohmann::json;
  const json doc = json::parse(meta_json_text.begin(), meta_json_text.end(), nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return bad_format("meta is not a valid JSON object");

  const auto global = doc.find("global");
  if (global == doc.end() || !global->is_object()) return bad_format("missing global");

  const auto datatype = global->find("core:datatype");
  if (datatype == global->end() || !datatype->is_string()) return bad_format("missing global.core:datatype");
  IqMeta meta;
  const std::string type_name = datatype->get<std::string>();
  if (type_name == "cu8") {
    meta.datatype = IqDatatype::cu8;
  } else if (type_name == "cf32_le") {
    meta.datatype = IqDatatype::cf32_le;
  } else {
    return bad_format("unsupported core:datatype '" + type_name + "'");
  }

  const auto rate = global->find("core:sample_rate");
  if (rate == global->end() || !is_finite_number(*rate) || rate->get<double>() <= 0.0) {
    return bad_format("missing or invalid global.core:sample_rate");
  }
  meta.sample_rate_sps = rate->get<double>();

  const auto captures = doc.find("captures");
  if (captures == doc.end() || !captures->is_array() || captures->empty() || !(*captures)[0].is_object()) {
    return bad_format("missing captures[0]");
  }
  const auto frequency = (*captures)[0].find("core:frequency");
  if (frequency == (*captures)[0].end() || !is_finite_number(*frequency)) {
    return bad_format("missing captures[0].core:frequency");
  }
  meta.center_freq_hz = frequency->get<double>();
  return meta;
}

void convert_cu8_to_cf32(std::span<const std::uint8_t> iq_bytes, std::span<Cf32> out) {
  const std::size_t count = std::min(out.size(), iq_bytes.size() / 2);
  for (std::size_t n = 0; n < count; ++n) {
    const float i = (static_cast<float>(iq_bytes[2 * n]) - 127.5f) / 127.5f;
    const float q = (static_cast<float>(iq_bytes[2 * n + 1]) - 127.5f) / 127.5f;
    out[n] = Cf32(i, q);
  }
}

std::variant<std::unique_ptr<SigmfReader>, IqOpenError> SigmfReader::open(const std::filesystem::path& meta_path,
                                                                          bool loop) {
  std::error_code ec;
  if (!std::filesystem::exists(meta_path, ec)) return IqOpenError{"not_found", meta_path.string() + " does not exist"};
  const auto meta_size = std::filesystem::is_regular_file(meta_path, ec) ? std::filesystem::file_size(meta_path, ec) : 0;
  if (ec || meta_size == 0 || meta_size > kMaxMetaBytes) return bad_format("meta is not a readable regular file");

  std::ifstream meta_file(meta_path, std::ios::binary);
  if (!meta_file) return bad_format("cannot read " + meta_path.string());
  const std::string meta_text((std::istreambuf_iterator<char>(meta_file)), std::istreambuf_iterator<char>());
  auto parsed = parse_sigmf_meta(meta_text);
  if (auto* error = std::get_if<IqOpenError>(&parsed)) return std::move(*error);

  std::filesystem::path data_path = meta_path;
  data_path.replace_extension(".sigmf-data");
  if (!std::filesystem::exists(data_path, ec)) return IqOpenError{"not_found", data_path.string() + " does not exist"};
  if (!std::filesystem::is_regular_file(data_path, ec)) return bad_format("data is not a regular file");

  std::unique_ptr<SigmfReader> reader(new SigmfReader());
  reader->meta_ = std::get<IqMeta>(parsed);
  reader->loop_ = loop;
  reader->data_.open(data_path, std::ios::binary);
  if (!reader->data_) return bad_format("cannot read " + data_path.string());
  reader->raw_bytes_.resize(kScratchSamples * bytes_per_sample(reader->meta_.datatype));
  return reader;
}

std::size_t SigmfReader::read_some(std::span<Cf32> out) {
  const std::size_t sample_bytes = bytes_per_sample(meta_.datatype);
  const std::size_t want = std::min(out.size(), kScratchSamples);
  data_.read(reinterpret_cast<char*>(raw_bytes_.data()), static_cast<std::streamsize>(want * sample_bytes));
  const std::size_t got = static_cast<std::size_t>(data_.gcount()) / sample_bytes;  // drops a torn tail
  if (meta_.datatype == IqDatatype::cu8) {
    convert_cu8_to_cf32({raw_bytes_.data(), got * 2}, out.first(got));
  } else {
    std::memcpy(out.data(), raw_bytes_.data(), got * sample_bytes);
  }
  if (data_.bad()) failed_ = true;
  return got;
}

std::size_t SigmfReader::read(std::span<Cf32> out) {
  std::size_t done = 0;
  bool just_rewound = false;
  while (done < out.size() && !failed_) {
    const std::size_t got = read_some(out.subspan(done));
    done += got;
    if (got > 0) {
      just_rewound = false;
      continue;
    }
    if (!loop_ || just_rewound || failed_) break;  // EOF, or an empty file while looping
    data_.clear();
    data_.seekg(0);
    just_rewound = true;
  }
  return done;
}

}  // namespace bl
