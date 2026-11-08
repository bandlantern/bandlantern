#include "api/token.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#else
#include <stdlib.h>
#endif

namespace bl::api {
namespace {

void fill_random(std::uint8_t* buf, std::size_t len) {
#if defined(_WIN32)
  if (BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    throw std::runtime_error("BCryptGenRandom failed");
#elif defined(__linux__)
  std::size_t done = 0;
  while (done < len) {
    const ssize_t n = getrandom(buf + done, len - done, 0);
    if (n < 0) throw std::runtime_error("getrandom failed");
    done += static_cast<std::size_t>(n);
  }
#else
  arc4random_buf(buf, len);  // macOS/BSD: kernel CSPRNG, cannot fail
#endif
}

}  // namespace

// RFC 4648 section 5 (URL-safe alphabet), padding omitted.
std::string base64url_encode(std::string_view bytes) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((bytes.size() * 4 + 2) / 3);
  std::size_t i = 0;
  for (; i + 3 <= bytes.size(); i += 3) {
    const std::uint32_t v = (std::uint32_t(std::uint8_t(bytes[i])) << 16) |
                            (std::uint32_t(std::uint8_t(bytes[i + 1])) << 8) |
                            std::uint32_t(std::uint8_t(bytes[i + 2]));
    for (int s = 18; s >= 0; s -= 6) out.push_back(kAlphabet[(v >> s) & 63]);
  }
  const std::size_t rest = bytes.size() - i;
  if (rest == 1) {
    const std::uint32_t v = std::uint32_t(std::uint8_t(bytes[i])) << 16;
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
  } else if (rest == 2) {
    const std::uint32_t v = (std::uint32_t(std::uint8_t(bytes[i])) << 16) |
                            (std::uint32_t(std::uint8_t(bytes[i + 1])) << 8);
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(kAlphabet[(v >> 6) & 63]);
  }
  return out;
}

std::string generate_token() {
  std::uint8_t raw[32];
  fill_random(raw, sizeof raw);
  return base64url_encode(std::string_view(reinterpret_cast<const char*>(raw), sizeof raw));
}

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
  const std::size_t n = a.size() > b.size() ? a.size() : b.size();
  std::uint8_t diff = a.size() != b.size() ? 1 : 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t x = i < a.size() ? std::uint8_t(a[i]) : 0;
    const std::uint8_t y = i < b.size() ? std::uint8_t(b[i]) : 0;
    diff |= static_cast<std::uint8_t>(x ^ y);
  }
  return diff == 0;
}

}  // namespace bl::api
