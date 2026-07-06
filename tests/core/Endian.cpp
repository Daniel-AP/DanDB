#include <catch_amalgamated.hpp>

#include <dandb/core/Endian.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

using dandb::core::read_u16_le;
using dandb::core::read_u32_le;
using dandb::core::read_u64_le;
using dandb::core::StatusCode;
using dandb::core::write_u16_be;
using dandb::core::write_u16_le;
using dandb::core::write_u32_be;
using dandb::core::write_u32_le;
using dandb::core::write_u64_be;
using dandb::core::write_u64_le;

TEST_CASE("write helpers store integers in little-endian byte order", "[core][endian]") {
    SECTION("u16") {
        std::array<std::byte, 2> bytes{};

        const auto status = write_u16_le(bytes, 0, 0x1234);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0x34 });
        REQUIRE(bytes[1] == std::byte{ 0x12 });
    }

    SECTION("u32") {
        std::array<std::byte, 4> bytes{};

        const auto status = write_u32_le(bytes, 0, 0x12345678);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0x78 });
        REQUIRE(bytes[1] == std::byte{ 0x56 });
        REQUIRE(bytes[2] == std::byte{ 0x34 });
        REQUIRE(bytes[3] == std::byte{ 0x12 });
    }

    SECTION("u64") {
        std::array<std::byte, 8> bytes{};

        const auto status = write_u64_le(bytes, 0, 0x0123456789ABCDEF);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0xEF });
        REQUIRE(bytes[1] == std::byte{ 0xCD });
        REQUIRE(bytes[2] == std::byte{ 0xAB });
        REQUIRE(bytes[3] == std::byte{ 0x89 });
        REQUIRE(bytes[4] == std::byte{ 0x67 });
        REQUIRE(bytes[5] == std::byte{ 0x45 });
        REQUIRE(bytes[6] == std::byte{ 0x23 });
        REQUIRE(bytes[7] == std::byte{ 0x01 });
    }
}

TEST_CASE("write helpers store integers in big-endian byte order", "[core][endian]") {
    SECTION("u16") {
        std::array<std::byte, 2> bytes{};

        const auto status = write_u16_be(bytes, 0, 0x1234);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0x12 });
        REQUIRE(bytes[1] == std::byte{ 0x34 });
    }

    SECTION("u32") {
        std::array<std::byte, 4> bytes{};

        const auto status = write_u32_be(bytes, 0, 0x12345678);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0x12 });
        REQUIRE(bytes[1] == std::byte{ 0x34 });
        REQUIRE(bytes[2] == std::byte{ 0x56 });
        REQUIRE(bytes[3] == std::byte{ 0x78 });
    }

    SECTION("u64") {
        std::array<std::byte, 8> bytes{};

        const auto status = write_u64_be(bytes, 0, 0x0123456789ABCDEF);

        REQUIRE(status.ok());
        REQUIRE(bytes[0] == std::byte{ 0x01 });
        REQUIRE(bytes[1] == std::byte{ 0x23 });
        REQUIRE(bytes[2] == std::byte{ 0x45 });
        REQUIRE(bytes[3] == std::byte{ 0x67 });
        REQUIRE(bytes[4] == std::byte{ 0x89 });
        REQUIRE(bytes[5] == std::byte{ 0xAB });
        REQUIRE(bytes[6] == std::byte{ 0xCD });
        REQUIRE(bytes[7] == std::byte{ 0xEF });
    }
}

TEST_CASE("read helpers parse integers from little-endian byte order", "[core][endian]") {
    SECTION("u16") {
        std::array bytes{
            std::byte{ 0x34 },
            std::byte{ 0x12 },
        };

        const auto result = read_u16_le(bytes, 0);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0x1234);
    }

    SECTION("u32") {
        std::array bytes{
            std::byte{ 0x78 },
            std::byte{ 0x56 },
            std::byte{ 0x34 },
            std::byte{ 0x12 },
        };

        const auto result = read_u32_le(bytes, 0);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0x12345678);
    }

    SECTION("u64") {
        std::array bytes{
            std::byte{ 0xEF },
            std::byte{ 0xCD },
            std::byte{ 0xAB },
            std::byte{ 0x89 },
            std::byte{ 0x67 },
            std::byte{ 0x45 },
            std::byte{ 0x23 },
            std::byte{ 0x01 },
        };

        const auto result = read_u64_le(bytes, 0);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0x0123456789ABCDEF);
    }
}

TEST_CASE("integer helpers round-trip each unsigned size", "[core][endian]") {
    SECTION("u16") {
        std::array<std::byte, 4> bytes{};

        REQUIRE(write_u16_le(bytes, 1, 0xBEEF).ok());
        const auto result = read_u16_le(bytes, 1);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0xBEEF);
    }

    SECTION("u32") {
        std::array<std::byte, 6> bytes{};

        REQUIRE(write_u32_le(bytes, 1, 0xDEADBEEF).ok());
        const auto result = read_u32_le(bytes, 1);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0xDEADBEEF);
    }

    SECTION("u64") {
        std::array<std::byte, 10> bytes{};

        REQUIRE(write_u64_le(bytes, 1, 0x0123456789ABCDEF).ok());
        const auto result = read_u64_le(bytes, 1);

        REQUIRE(result.ok());
        REQUIRE(result.value() == 0x0123456789ABCDEF);
    }
}

TEST_CASE("write helpers reject buffers that are too small", "[core][endian]") {
    SECTION("u16") {
        std::array bytes{
            std::byte{ 0xAA },
        };

        const auto status = write_u16_le(bytes, 0, 0x1234);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
        REQUIRE(bytes[0] == std::byte{ 0xAA });
    }

    SECTION("u32") {
        std::array bytes{
            std::byte{ 0xAA },
            std::byte{ 0xBB },
            std::byte{ 0xCC },
        };

        const auto status = write_u32_le(bytes, 0, 0x12345678);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
        REQUIRE(bytes[0] == std::byte{ 0xAA });
        REQUIRE(bytes[1] == std::byte{ 0xBB });
        REQUIRE(bytes[2] == std::byte{ 0xCC });
    }

    SECTION("u64") {
        std::array bytes{
            std::byte{ 0xAA },
            std::byte{ 0xBB },
            std::byte{ 0xCC },
            std::byte{ 0xDD },
            std::byte{ 0xEE },
            std::byte{ 0xFF },
            std::byte{ 0x11 },
        };

        const auto status = write_u64_le(bytes, 0, 0x0123456789ABCDEF);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
        REQUIRE(bytes[0] == std::byte{ 0xAA });
        REQUIRE(bytes[1] == std::byte{ 0xBB });
        REQUIRE(bytes[2] == std::byte{ 0xCC });
        REQUIRE(bytes[3] == std::byte{ 0xDD });
        REQUIRE(bytes[4] == std::byte{ 0xEE });
        REQUIRE(bytes[5] == std::byte{ 0xFF });
        REQUIRE(bytes[6] == std::byte{ 0x11 });
    }
}

TEST_CASE("read helpers reject buffers that are too small", "[core][endian]") {
    SECTION("u16") {
        constexpr std::array bytes{
            std::byte{ 0x34 },
        };

        const auto result = read_u16_le(bytes, 0);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("u32") {
        constexpr std::array bytes{
            std::byte{ 0x78 },
            std::byte{ 0x56 },
            std::byte{ 0x34 },
        };

        const auto result = read_u32_le(bytes, 0);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("u64") {
        constexpr std::array bytes{
            std::byte{ 0xEF },
            std::byte{ 0xCD },
            std::byte{ 0xAB },
            std::byte{ 0x89 },
            std::byte{ 0x67 },
            std::byte{ 0x45 },
            std::byte{ 0x23 },
        };

        const auto result = read_u64_le(bytes, 0);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("helpers reject offsets that cannot fit the requested integer", "[core][endian]") {
    std::array<std::byte, 8> bytes{};
    const auto huge_offset = std::numeric_limits<std::size_t>::max();

    REQUIRE_FALSE(write_u16_le(bytes, huge_offset, 0x1234).ok());
    REQUIRE_FALSE(write_u32_le(bytes, huge_offset, 0x12345678).ok());
    REQUIRE_FALSE(write_u64_le(bytes, huge_offset, 0x0123456789ABCDEF).ok());

    REQUIRE_FALSE(read_u16_le(bytes, huge_offset).ok());
    REQUIRE_FALSE(read_u32_le(bytes, huge_offset).ok());
    REQUIRE_FALSE(read_u64_le(bytes, huge_offset).ok());
}
