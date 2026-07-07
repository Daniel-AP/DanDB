#include <catch_amalgamated.hpp>

#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/wal/WalHeader.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using dandb::core::PAGE_SIZE;
using dandb::core::StatusCode;
using dandb::core::checksum;
using dandb::core::read_u32_le;
using dandb::core::read_u64_le;
using dandb::core::write_u32_le;
using dandb::core::write_u64_le;
using dandb::wal::WAL_CHECKSUM_OFFSET;
using dandb::wal::WAL_DATABASE_ID_OFFSET;
using dandb::wal::WAL_FORMAT_VERSION;
using dandb::wal::WAL_FORMAT_VERSION_OFFSET;
using dandb::wal::WAL_HEADER_RESERVED_BYTES_SIZE;
using dandb::wal::WAL_HEADER_SIZE;
using dandb::wal::WAL_HEADER_SIZE_OFFSET;
using dandb::wal::WAL_MAGIC_BYTES;
using dandb::wal::WAL_MAGIC_BYTES_OFFSET;
using dandb::wal::WAL_PAGE_SIZE_OFFSET;
using dandb::wal::WAL_RESERVED_BYTES_OFFSET;
using dandb::wal::WalHeader;

namespace {

    using WalHeaderBytes = std::array<std::byte, WAL_HEADER_SIZE>;

    constexpr std::uint64_t DATABASE_ID = 0x0102030405060708ULL;

    WalHeaderBytes make_valid_header_bytes() {
        WalHeaderBytes bytes{};

        for(std::size_t i = 0; i < WAL_MAGIC_BYTES.size(); i++) {
            bytes[i] = WAL_MAGIC_BYTES[i];
        }

        REQUIRE(write_u32_le(bytes, 4, WAL_FORMAT_VERSION).ok());
        REQUIRE(write_u32_le(bytes, 8, static_cast<std::uint32_t>(PAGE_SIZE)).ok());
        REQUIRE(write_u32_le(bytes, 12, WAL_HEADER_SIZE).ok());
        REQUIRE(write_u64_le(bytes, 16, DATABASE_ID).ok());

        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_HEADER_SIZE - sizeof(std::uint64_t)));
        REQUIRE(write_u64_le(bytes, 56, current_checksum).ok());

        return bytes;
    }

    void rewrite_header_checksum(WalHeaderBytes& bytes) {
        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_HEADER_SIZE - sizeof(std::uint64_t)));

        REQUIRE(write_u64_le(bytes, 56, current_checksum).ok());
    }

    void require_corruption(const WalHeaderBytes& bytes) {
        const auto result = WalHeader::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::Corruption);
    }

}

TEST_CASE("wal header exposes named layout offsets", "[wal][wal-header]") {
    static_assert(WAL_MAGIC_BYTES_OFFSET == 0);
    static_assert(WAL_FORMAT_VERSION_OFFSET == 4);
    static_assert(WAL_PAGE_SIZE_OFFSET == 8);
    static_assert(WAL_HEADER_SIZE_OFFSET == 12);
    static_assert(WAL_DATABASE_ID_OFFSET == 16);
    static_assert(WAL_RESERVED_BYTES_OFFSET == 24);
    static_assert(WAL_CHECKSUM_OFFSET == 56);
    static_assert(WAL_CHECKSUM_OFFSET + sizeof(std::uint64_t) == WAL_HEADER_SIZE);
    static_assert(WAL_CHECKSUM_OFFSET - WAL_RESERVED_BYTES_OFFSET == WAL_HEADER_RESERVED_BYTES_SIZE);
}

TEST_CASE("wal header decodes a valid documented header", "[wal][wal-header]") {
    const auto bytes = make_valid_header_bytes();

    const auto result = WalHeader::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.value().database_id() == DATABASE_ID);
}

TEST_CASE("wal header encodes a new header into the documented layout", "[wal][wal-header]") {
    WalHeaderBytes bytes{};
    bytes.fill(std::byte{ 0xAA });

    const auto header = WalHeader::create_new(DATABASE_ID);

    const auto status = header.encode_into(bytes);

    REQUIRE(status.ok());
    REQUIRE(bytes[0] == WAL_MAGIC_BYTES[0]);
    REQUIRE(bytes[1] == WAL_MAGIC_BYTES[1]);
    REQUIRE(bytes[2] == WAL_MAGIC_BYTES[2]);
    REQUIRE(bytes[3] == WAL_MAGIC_BYTES[3]);

    const auto version = read_u32_le(bytes, 4);
    REQUIRE(version.ok());
    REQUIRE(version.value() == WAL_FORMAT_VERSION);

    const auto page_size = read_u32_le(bytes, 8);
    REQUIRE(page_size.ok());
    REQUIRE(page_size.value() == PAGE_SIZE);

    const auto header_size = read_u32_le(bytes, 12);
    REQUIRE(header_size.ok());
    REQUIRE(header_size.value() == WAL_HEADER_SIZE);

    const auto database_id = read_u64_le(bytes, 16);
    REQUIRE(database_id.ok());
    REQUIRE(database_id.value() == DATABASE_ID);

    for(std::size_t offset = 24; offset < 56; offset++) {
        REQUIRE(bytes[offset] == std::byte{ 0 });
    }

    const auto stored_checksum = read_u64_le(bytes, 56);
    REQUIRE(stored_checksum.ok());
    REQUIRE(stored_checksum.value() == checksum(std::span<const std::byte>(bytes).first(WAL_HEADER_SIZE - sizeof(std::uint64_t))));
}

TEST_CASE("wal header refuses buffers that are not exactly the header size", "[wal][wal-header]") {
    SECTION("encode") {
        std::array<std::byte, WAL_HEADER_SIZE - 1> bytes{};
        const auto header = WalHeader::create_new(DATABASE_ID);

        const auto status = header.encode_into(bytes);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("decode") {
        std::array<std::byte, WAL_HEADER_SIZE - 1> bytes{};

        const auto result = WalHeader::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("wal header rejects invalid fixed fields while decoding", "[wal][wal-header]") {
    SECTION("bad magic") {
        auto bytes = make_valid_header_bytes();
        bytes[0] = std::byte{ 'X' };
        rewrite_header_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported format version") {
        auto bytes = make_valid_header_bytes();
        REQUIRE(write_u32_le(bytes, 4, WAL_FORMAT_VERSION + 1).ok());
        rewrite_header_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported page size") {
        auto bytes = make_valid_header_bytes();
        REQUIRE(write_u32_le(bytes, 8, static_cast<std::uint32_t>(PAGE_SIZE * 2)).ok());
        rewrite_header_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported header size") {
        auto bytes = make_valid_header_bytes();
        REQUIRE(write_u32_le(bytes, 12, WAL_HEADER_SIZE + 8).ok());
        rewrite_header_checksum(bytes);

        require_corruption(bytes);
    }
}

TEST_CASE("wal header rejects nonzero reserved bytes", "[wal][wal-header]") {
    auto bytes = make_valid_header_bytes();

    bytes[24] = std::byte{ 0x01 };
    rewrite_header_checksum(bytes);

    require_corruption(bytes);
}

TEST_CASE("wal header rejects a bad checksum", "[wal][wal-header]") {
    auto bytes = make_valid_header_bytes();

    REQUIRE(write_u64_le(bytes, 56, 0).ok());

    require_corruption(bytes);
}
