#include <catch_amalgamated.hpp>

#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>
#include <dandb/wal/WalPageFrame.h>

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
using dandb::storage::PageId;
using dandb::wal::WAL_PAGE_FRAME_RECORD_SIZE;
using dandb::wal::WAL_PAGE_FRAME_RECORD_TYPE;
using dandb::wal::WalPageFrame;

namespace {

    using WalPageFrameBytes = std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE>;

    constexpr std::uint64_t TRANSACTION_ID = 0x0102030405060708ULL;
    constexpr PageId PAGE_ID{ 7 };

    std::array<std::byte, PAGE_SIZE> make_page_image() {
        std::array<std::byte, PAGE_SIZE> page_image{};

        for(std::size_t i = 0; i < page_image.size(); i++) {
            page_image[i] = static_cast<std::byte>(i % 251);
        }

        return page_image;
    }

    WalPageFrameBytes make_valid_page_frame_bytes() {
        WalPageFrameBytes bytes{};
        const auto page_image = make_page_image();

        REQUIRE(write_u32_le(bytes, 0, WAL_PAGE_FRAME_RECORD_TYPE).ok());
        REQUIRE(write_u32_le(bytes, 4, WAL_PAGE_FRAME_RECORD_SIZE).ok());
        REQUIRE(write_u64_le(bytes, 8, TRANSACTION_ID).ok());
        REQUIRE(write_u64_le(bytes, 16, PAGE_ID.id).ok());
        REQUIRE(write_u32_le(bytes, 24, static_cast<std::uint32_t>(PAGE_SIZE)).ok());

        for(std::size_t i = 0; i < page_image.size(); i++) {
            bytes[32+i] = page_image[i];
        }

        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_PAGE_FRAME_RECORD_SIZE - sizeof(std::uint64_t)));
        REQUIRE(write_u64_le(bytes, 4128, current_checksum).ok());

        return bytes;
    }

    void rewrite_page_frame_checksum(WalPageFrameBytes& bytes) {
        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_PAGE_FRAME_RECORD_SIZE - sizeof(std::uint64_t)));

        REQUIRE(write_u64_le(bytes, 4128, current_checksum).ok());
    }

    void require_corruption(const WalPageFrameBytes& bytes) {
        const auto result = WalPageFrame::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::Corruption);
    }

}

TEST_CASE("wal page frame decodes a valid documented frame", "[wal][wal-page-frame]") {
    const auto bytes = make_valid_page_frame_bytes();
    const auto expected_page_image = make_page_image();

    const auto result = WalPageFrame::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(result.value().page_id() == PAGE_ID);
    REQUIRE(result.value().page_image() == expected_page_image);
}

TEST_CASE("wal page frame encodes into the documented layout", "[wal][wal-page-frame]") {
    WalPageFrameBytes bytes{};
    bytes.fill(std::byte{ 0xAA });

    const auto page_image = make_page_image();
    const auto frame = WalPageFrame::create_new(TRANSACTION_ID, PAGE_ID, page_image);

    const auto status = frame.encode_into(bytes);

    REQUIRE(status.ok());

    const auto record_type = read_u32_le(bytes, 0);
    REQUIRE(record_type.ok());
    REQUIRE(record_type.value() == WAL_PAGE_FRAME_RECORD_TYPE);

    const auto record_size = read_u32_le(bytes, 4);
    REQUIRE(record_size.ok());
    REQUIRE(record_size.value() == WAL_PAGE_FRAME_RECORD_SIZE);

    const auto transaction_id = read_u64_le(bytes, 8);
    REQUIRE(transaction_id.ok());
    REQUIRE(transaction_id.value() == TRANSACTION_ID);

    const auto page_id = read_u64_le(bytes, 16);
    REQUIRE(page_id.ok());
    REQUIRE(page_id.value() == PAGE_ID.id);

    const auto page_image_size = read_u32_le(bytes, 24);
    REQUIRE(page_image_size.ok());
    REQUIRE(page_image_size.value() == PAGE_SIZE);

    const auto reserved = read_u32_le(bytes, 28);
    REQUIRE(reserved.ok());
    REQUIRE(reserved.value() == 0);

    for(std::size_t i = 0; i < PAGE_SIZE; i++) {
        REQUIRE(bytes[32+i] == page_image[i]);
    }

    const auto stored_checksum = read_u64_le(bytes, 4128);
    REQUIRE(stored_checksum.ok());
    REQUIRE(stored_checksum.value() == checksum(std::span<const std::byte>(bytes).first(WAL_PAGE_FRAME_RECORD_SIZE - sizeof(std::uint64_t))));
}

TEST_CASE("wal page frame refuses buffers that are not exactly the record size", "[wal][wal-page-frame]") {
    SECTION("encode") {
        std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE - 1> bytes{};
        const auto frame = WalPageFrame::create_new(TRANSACTION_ID, PAGE_ID, make_page_image());

        const auto status = frame.encode_into(bytes);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("decode") {
        std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE - 1> bytes{};

        const auto result = WalPageFrame::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("wal page frame rejects invalid fixed fields while decoding", "[wal][wal-page-frame]") {
    SECTION("unknown record type") {
        auto bytes = make_valid_page_frame_bytes();
        REQUIRE(write_u32_le(bytes, 0, WAL_PAGE_FRAME_RECORD_TYPE + 1).ok());
        rewrite_page_frame_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported record size") {
        auto bytes = make_valid_page_frame_bytes();
        REQUIRE(write_u32_le(bytes, 4, WAL_PAGE_FRAME_RECORD_SIZE + 8).ok());
        rewrite_page_frame_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported page image size") {
        auto bytes = make_valid_page_frame_bytes();
        REQUIRE(write_u32_le(bytes, 24, static_cast<std::uint32_t>(PAGE_SIZE / 2)).ok());
        rewrite_page_frame_checksum(bytes);

        require_corruption(bytes);
    }
}

TEST_CASE("wal page frame rejects nonzero reserved bytes", "[wal][wal-page-frame]") {
    auto bytes = make_valid_page_frame_bytes();

    bytes[28] = std::byte{ 0x01 };
    rewrite_page_frame_checksum(bytes);

    require_corruption(bytes);
}

TEST_CASE("wal page frame rejects a bad checksum", "[wal][wal-page-frame]") {
    auto bytes = make_valid_page_frame_bytes();

    REQUIRE(write_u64_le(bytes, 4128, 0).ok());

    require_corruption(bytes);
}
