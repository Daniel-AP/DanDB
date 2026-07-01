#include <catch_amalgamated.hpp>

#include <dandb/core/Checksum.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/wal/WalCommitRecord.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using dandb::core::StatusCode;
using dandb::core::checksum;
using dandb::core::read_u32_le;
using dandb::core::read_u64_le;
using dandb::core::write_u32_le;
using dandb::core::write_u64_le;
using dandb::wal::WAL_COMMIT_RECORD_SIZE;
using dandb::wal::WAL_COMMIT_RECORD_TYPE;
using dandb::wal::WalCommitRecord;

namespace {

    using WalCommitRecordBytes = std::array<std::byte, WAL_COMMIT_RECORD_SIZE>;

    constexpr std::uint64_t TRANSACTION_ID = 0x0102030405060708ULL;
    constexpr std::uint64_t FRAME_COUNT = 3;

    WalCommitRecordBytes make_valid_commit_record_bytes() {
        WalCommitRecordBytes bytes{};

        REQUIRE(write_u32_le(bytes, 0, WAL_COMMIT_RECORD_TYPE).ok());
        REQUIRE(write_u32_le(bytes, 4, WAL_COMMIT_RECORD_SIZE).ok());
        REQUIRE(write_u64_le(bytes, 8, TRANSACTION_ID).ok());
        REQUIRE(write_u64_le(bytes, 16, FRAME_COUNT).ok());

        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_COMMIT_RECORD_SIZE - sizeof(std::uint64_t)));
        REQUIRE(write_u64_le(bytes, 24, current_checksum).ok());

        return bytes;
    }

    void rewrite_commit_record_checksum(WalCommitRecordBytes& bytes) {
        const auto current_checksum = checksum(std::span<const std::byte>(bytes).first(WAL_COMMIT_RECORD_SIZE - sizeof(std::uint64_t)));

        REQUIRE(write_u64_le(bytes, 24, current_checksum).ok());
    }

    void require_corruption(const WalCommitRecordBytes& bytes) {
        const auto result = WalCommitRecord::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::Corruption);
    }

}

TEST_CASE("wal commit record decodes a valid documented record", "[wal][wal-commit-record]") {
    const auto bytes = make_valid_commit_record_bytes();

    const auto result = WalCommitRecord::decode(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(result.value().frame_count() == FRAME_COUNT);
}

TEST_CASE("wal commit record encodes into the documented layout", "[wal][wal-commit-record]") {
    WalCommitRecordBytes bytes{};
    bytes.fill(std::byte{ 0xAA });

    const auto record = WalCommitRecord::create_new(TRANSACTION_ID, FRAME_COUNT);

    const auto status = record.encode_into(bytes);

    REQUIRE(status.ok());

    const auto record_type = read_u32_le(bytes, 0);
    REQUIRE(record_type.ok());
    REQUIRE(record_type.value() == WAL_COMMIT_RECORD_TYPE);

    const auto record_size = read_u32_le(bytes, 4);
    REQUIRE(record_size.ok());
    REQUIRE(record_size.value() == WAL_COMMIT_RECORD_SIZE);

    const auto transaction_id = read_u64_le(bytes, 8);
    REQUIRE(transaction_id.ok());
    REQUIRE(transaction_id.value() == TRANSACTION_ID);

    const auto frame_count = read_u64_le(bytes, 16);
    REQUIRE(frame_count.ok());
    REQUIRE(frame_count.value() == FRAME_COUNT);

    const auto stored_checksum = read_u64_le(bytes, 24);
    REQUIRE(stored_checksum.ok());
    REQUIRE(stored_checksum.value() == checksum(std::span<const std::byte>(bytes).first(WAL_COMMIT_RECORD_SIZE - sizeof(std::uint64_t))));
}

TEST_CASE("wal commit record refuses buffers that are not exactly the record size", "[wal][wal-commit-record]") {
    SECTION("encode") {
        std::array<std::byte, WAL_COMMIT_RECORD_SIZE - 1> bytes{};
        const auto record = WalCommitRecord::create_new(TRANSACTION_ID, FRAME_COUNT);

        const auto status = record.encode_into(bytes);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("decode") {
        std::array<std::byte, WAL_COMMIT_RECORD_SIZE - 1> bytes{};

        const auto result = WalCommitRecord::decode(bytes);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("wal commit record rejects invalid fixed fields while decoding", "[wal][wal-commit-record]") {
    SECTION("unknown record type") {
        auto bytes = make_valid_commit_record_bytes();
        REQUIRE(write_u32_le(bytes, 0, WAL_COMMIT_RECORD_TYPE + 1).ok());
        rewrite_commit_record_checksum(bytes);

        require_corruption(bytes);
    }

    SECTION("unsupported record size") {
        auto bytes = make_valid_commit_record_bytes();
        REQUIRE(write_u32_le(bytes, 4, WAL_COMMIT_RECORD_SIZE + 8).ok());
        rewrite_commit_record_checksum(bytes);

        require_corruption(bytes);
    }
}

TEST_CASE("wal commit record rejects a bad checksum", "[wal][wal-commit-record]") {
    auto bytes = make_valid_commit_record_bytes();

    REQUIRE(write_u64_le(bytes, 24, 0).ok());

    require_corruption(bytes);
}
