#include <catch_amalgamated.hpp>

#include <dandb/core/Checksum.h>

#include <array>
#include <cstddef>
#include <cstdint>

TEST_CASE("checksum returns the FNV-1a offset basis for empty input", "[core][checksum]") {
    constexpr std::array<std::byte, 0> bytes{};

    const auto value = dandb::core::checksum(bytes);

    REQUIRE(value == 0xcbf29ce484222325ULL);
}

TEST_CASE("checksum matches a known FNV-1a value", "[core][checksum]") {
    constexpr std::array bytes{
        std::byte{ 0x68 },
        std::byte{ 0x65 },
        std::byte{ 0x6C },
        std::byte{ 0x6C },
        std::byte{ 0x6F },
    };

    const auto value = dandb::core::checksum(bytes);

    REQUIRE(value == 0xa430d84680aabd0bULL);
}

TEST_CASE("checksum changes when one byte changes", "[core][checksum]") {
    constexpr std::array original{
        std::byte{ 0x63 },
        std::byte{ 0x61 },
        std::byte{ 0x74 },
    };
    constexpr std::array changed{
        std::byte{ 0x63 },
        std::byte{ 0x61 },
        std::byte{ 0x72 },
    };

    REQUIRE(dandb::core::checksum(original) != dandb::core::checksum(changed));
}

TEST_CASE("checksum uses the provided seed as the initial value", "[core][checksum]") {
    constexpr std::array<std::byte, 0> bytes{};
    constexpr auto seed = 0x123456789abcdef0ULL;

    const auto value = dandb::core::checksum(bytes, seed);

    REQUIRE(value == seed);
}
