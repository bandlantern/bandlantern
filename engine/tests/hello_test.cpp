// Hello-world test: proves Catch2 is found via vcpkg and discovered by ctest.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

TEST_CASE("C++20 std::span views a cu8 sample buffer", "[toolchain]") {
    const std::array<std::uint8_t, 4> samples_cu8{127, 128, 0, 255};
    const std::span<const std::uint8_t> view{samples_cu8};

    REQUIRE(view.size() == 4);
    REQUIRE(view.back() == 255);
}
