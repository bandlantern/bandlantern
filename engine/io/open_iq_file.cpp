#include <algorithm>
#include <cctype>
#include <string>

#include "io/iq_source.hpp"
#include "io/sigmf.hpp"
#include "io/wav.hpp"

namespace bl {

namespace {

std::string lower_extension(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

template <typename Reader>
IqOpenResult wrap(std::variant<std::unique_ptr<Reader>, IqOpenError> opened) {
  if (auto* error = std::get_if<IqOpenError>(&opened)) return std::move(*error);
  return std::unique_ptr<IIqSource>(std::move(std::get<std::unique_ptr<Reader>>(opened)));
}

}  // namespace

IqOpenResult open_iq_file(const std::filesystem::path& path, bool loop) {
  const std::string ext = lower_extension(path);
  if (ext == ".sigmf-meta") return wrap(SigmfReader::open(path, loop));
  if (ext == ".wav") return wrap(WavReader::open(path, loop));
  return IqOpenError{"bad_format", "not a .sigmf-meta or .wav file"};
}

}  // namespace bl
