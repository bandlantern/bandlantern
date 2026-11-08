#include "io/wav.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <string>
#include <system_error>

#include "io/sigmf.hpp"  // convert_cu8_to_cf32

namespace bl {

static_assert(std::endian::native == std::endian::little, "cf32_le is copied verbatim");

namespace {

constexpr std::size_t kScratchSamples = 16384;
constexpr std::size_t kChunkHeaderBytes = 8;  // 4-byte id + u32 LE size
constexpr std::size_t kFmtBaseBytes = 16;     // through wBitsPerSample
constexpr std::size_t kFmtExtensibleBytes = 40;

constexpr std::uint16_t kFormatPcm = 0x0001;
constexpr std::uint16_t kFormatIeeeFloat = 0x0003;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

// EXTENSIBLE SubFormat is a KSDATAFORMAT_SUBTYPE GUID {tttt0000-0000-0010-8000-00AA00389B71}
// whose first two bytes are the plain format tag; these are the other 14 bytes as stored.
constexpr std::array<std::uint8_t, 14> kSubFormatGuidTail = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                             0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

IqOpenError bad_format(std::string message) { return IqOpenError{"bad_format", std::move(message)}; }

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t le32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool read_at(std::istream& in, std::uint64_t pos, std::uint8_t* out, std::size_t bytes) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(pos));
  in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(bytes));
  return static_cast<std::size_t>(in.gcount()) == bytes;
}

struct WavFormat {
  IqDatatype datatype = IqDatatype::cu8;
  std::uint32_t rate_sps = 0;
  std::uint32_t block_align_bytes = 0;
};

// fmt chunk body (WAVEFORMATEX, optionally WAVEFORMATEXTENSIBLE). body holds up to 40 bytes.
std::variant<WavFormat, IqOpenError> parse_fmt(std::span<const std::uint8_t> body) {
  if (body.size() < kFmtBaseBytes) return bad_format("fmt chunk too short");
  std::uint16_t format_tag = le16(&body[0]);
  const std::uint16_t channels = le16(&body[2]);
  const std::uint32_t rate_sps = le32(&body[4]);
  const std::uint16_t block_align = le16(&body[12]);
  const std::uint16_t bits = le16(&body[14]);

  if (format_tag == kFormatExtensible) {
    if (body.size() < kFmtExtensibleBytes ||
        !std::equal(kSubFormatGuidTail.begin(), kSubFormatGuidTail.end(), &body[26])) {
      return bad_format("EXTENSIBLE fmt without a known SubFormat");
    }
    format_tag = le16(&body[24]);
  }
  if (channels != 2) return bad_format("WAV must be 2-channel IQ (got " + std::to_string(channels) + " channels)");
  if (rate_sps == 0) return bad_format("WAV sample rate is 0");

  WavFormat format;
  if (format_tag == kFormatPcm && bits == 8) {
    format.datatype = IqDatatype::cu8;
  } else if (format_tag == kFormatPcm && bits == 16) {
    format.datatype = IqDatatype::cs16_le;
  } else if (format_tag == kFormatIeeeFloat && bits == 32) {
    format.datatype = IqDatatype::cf32_le;
  } else {
    return bad_format("unsupported WAV format (tag " + std::to_string(format_tag) + ", " + std::to_string(bits) +
                      " bits)");
  }
  if (block_align != channels * bits / 8) return bad_format("WAV block align does not match channels and bits");
  format.rate_sps = rate_sps;
  format.block_align_bytes = block_align;
  return format;
}

bool all_digits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::variant<WavLayout, IqOpenError> parse_wav_header(std::istream& in) {
  in.clear();
  in.seekg(0, std::ios::end);
  const std::streamoff end = in.tellg();
  if (end < 0) return bad_format("WAV file is not seekable");
  const auto file_size = static_cast<std::uint64_t>(end);

  std::array<std::uint8_t, 12> riff{};
  if (!read_at(in, 0, riff.data(), riff.size())) return bad_format("file too short for a RIFF header");
  if (std::memcmp(riff.data(), "RF64", 4) == 0) return bad_format("RF64 (over 4 GB) WAV is not supported");
  if (std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(&riff[8], "WAVE", 4) != 0) {
    return bad_format("not a RIFF/WAVE file");
  }

  // The RIFF size field is ignored: recorders that stop abruptly leave it wrong; the file
  // length is the authority. Each step advances by at least 8 bytes, so the walk ends.
  std::optional<WavFormat> format;
  std::optional<std::uint64_t> data_offset;
  std::uint64_t data_size = 0;
  std::uint64_t pos = riff.size();
  while (pos + kChunkHeaderBytes <= file_size && !(format && data_offset)) {
    std::array<std::uint8_t, kChunkHeaderBytes> header{};
    if (!read_at(in, pos, header.data(), header.size())) break;
    const std::uint64_t body_pos = pos + kChunkHeaderBytes;
    const std::uint32_t size = le32(&header[4]);

    if (std::memcmp(header.data(), "fmt ", 4) == 0) {
      std::array<std::uint8_t, kFmtExtensibleBytes> body{};
      const std::size_t want = std::min<std::size_t>(size, body.size());
      if (!read_at(in, body_pos, body.data(), want)) return bad_format("fmt chunk truncated");
      auto parsed = parse_fmt({body.data(), want});
      if (auto* error = std::get_if<IqOpenError>(&parsed)) return std::move(*error);
      format = std::get<WavFormat>(parsed);
    } else if (std::memcmp(header.data(), "data", 4) == 0) {
      data_offset = body_pos;
      data_size = std::min<std::uint64_t>(size, file_size - body_pos);
    }
    pos = body_pos + size + (size & 1u);
  }
  if (!format) return bad_format("WAV has no fmt chunk");
  if (!data_offset) return bad_format("WAV has no data chunk");

  WavLayout layout;
  layout.meta.datatype = format->datatype;
  layout.meta.sample_rate_sps = static_cast<double>(format->rate_sps);
  layout.block_align_bytes = format->block_align_bytes;
  layout.data_offset_bytes = *data_offset;
  layout.data_size_bytes = data_size - data_size % format->block_align_bytes;
  return layout;
}

std::optional<double> center_freq_from_filename(std::string_view filename) {
  const std::size_t dot = filename.rfind('.');
  std::string_view rest = filename.substr(0, dot);
  while (!rest.empty()) {
    const std::size_t sep = rest.find('_');
    const std::string_view token = rest.substr(0, sep);
    if (token.size() > 2 && token.ends_with("Hz") && all_digits(token.substr(0, token.size() - 2))) {
      std::uint64_t hz = 0;
      const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size() - 2, hz);
      if (ec == std::errc()) return static_cast<double>(hz);
    }
    if (sep == std::string_view::npos) break;
    rest.remove_prefix(sep + 1);
  }
  return std::nullopt;
}

void convert_cs16_le_to_cf32(std::span<const std::uint8_t> iq_bytes, std::span<Cf32> out) {
  const std::size_t count = std::min(out.size(), iq_bytes.size() / 4);
  for (std::size_t n = 0; n < count; ++n) {
    const auto i = static_cast<std::int16_t>(le16(&iq_bytes[4 * n]));
    const auto q = static_cast<std::int16_t>(le16(&iq_bytes[4 * n + 2]));
    out[n] = Cf32(static_cast<float>(i) / 32768.0f, static_cast<float>(q) / 32768.0f);
  }
}

std::variant<std::unique_ptr<WavReader>, IqOpenError> WavReader::open(const std::filesystem::path& path, bool loop) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return IqOpenError{"not_found", path.string() + " does not exist"};
  if (!std::filesystem::is_regular_file(path, ec)) return bad_format("not a regular file");

  std::unique_ptr<WavReader> reader(new WavReader());
  reader->file_.open(path, std::ios::binary);
  if (!reader->file_) return bad_format("cannot read " + path.string());
  auto layout = parse_wav_header(reader->file_);
  if (auto* error = std::get_if<IqOpenError>(&layout)) return std::move(*error);
  reader->layout_ = std::get<WavLayout>(layout);
  reader->layout_.meta.center_freq_hz = center_freq_from_filename(path.filename().string()).value_or(0.0);
  reader->loop_ = loop;
  reader->raw_bytes_.resize(kScratchSamples * reader->layout_.block_align_bytes);
  reader->rewind();
  return reader;
}

void WavReader::rewind() {
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(layout_.data_offset_bytes));
  remaining_bytes_ = layout_.data_size_bytes;
}

std::size_t WavReader::read_some(std::span<Cf32> out) {
  const std::size_t frame_bytes = layout_.block_align_bytes;
  const std::size_t want =
      static_cast<std::size_t>(std::min<std::uint64_t>({out.size(), kScratchSamples, remaining_bytes_ / frame_bytes}));
  if (want == 0) return 0;
  file_.read(reinterpret_cast<char*>(raw_bytes_.data()), static_cast<std::streamsize>(want * frame_bytes));
  const std::size_t got = static_cast<std::size_t>(file_.gcount()) / frame_bytes;  // drops a torn tail
  // A short read means the file shrank since open(): treat what is left as gone.
  remaining_bytes_ = got < want ? 0 : remaining_bytes_ - got * frame_bytes;

  const std::span<const std::uint8_t> raw(raw_bytes_.data(), got * frame_bytes);
  switch (layout_.meta.datatype) {
    case IqDatatype::cu8: convert_cu8_to_cf32(raw, out.first(got)); break;
    case IqDatatype::cs16_le: convert_cs16_le_to_cf32(raw, out.first(got)); break;
    case IqDatatype::cf32_le: std::memcpy(out.data(), raw.data(), raw.size()); break;
  }
  if (file_.bad()) failed_ = true;
  return got;
}

std::size_t WavReader::read(std::span<Cf32> out) {
  std::size_t done = 0;
  bool just_rewound = false;
  while (done < out.size() && !failed_) {
    const std::size_t got = read_some(out.subspan(done));
    done += got;
    if (got > 0) {
      just_rewound = false;
      continue;
    }
    if (!loop_ || just_rewound || failed_) break;  // EOF, or an empty data chunk while looping
    rewind();
    just_rewound = true;
  }
  return done;
}

}  // namespace bl
