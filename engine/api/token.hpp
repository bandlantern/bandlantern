// Per-session auth token (README "Token"). The token must never be logged.
#pragma once

#include <string>
#include <string_view>

namespace bl::api {

// 32 bytes from the OS CSPRNG, base64url without padding (43 chars).
// Throws std::runtime_error if the OS source fails.
std::string generate_token();

// base64url (RFC 4648 section 5) without padding.
std::string base64url_encode(std::string_view bytes);

// Time depends only on the longer input's length, not on where they differ.
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;

}  // namespace bl::api
