#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>
#include <dandb/wal/WalCommitRecord.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalScanResult.h>
#include <dandb/wal/WalScanner.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

using dandb::core::PAGE_SIZE;
using dandb::core::StatusCode;
using dandb::core::write_u32_le;
using dandb::storage::Page;
using dandb::storage::PageId;
using dandb::wal::WAL_COMMIT_RECORD_SIZE;
using dandb::wal::WAL_HEADER_SIZE;
using dandb::wal::WAL_PAGE_FRAME_RECORD_SIZE;
using dandb::wal::WAL_PAGE_FRAME_RECORD_TYPE;
using dandb::wal::WalCommitRecord;
using dandb::wal::WalHeader;
using dandb::wal::WalPageFrame;
using dandb::wal::WalScanResult;
using dandb::wal::WalScanner;

namespace {

    constexpr std::uint64_t DATABASE_ID = 0x0102030405060708ULL;
    constexpr std::uint64_t OTHER_DATABASE_ID = 0x1112131415161718ULL;
    constexpr std::uint64_t TRANSACTION_ID_1 = 41;
    constexpr std::uint64_t TRANSACTION_ID_2 = 42;
    constexpr PageId PAGE_ID_1{ 7 };
    constexpr PageId PAGE_ID_2{ 8 };

    template<std::size_t Size>
    void write_file_bytes(
        const std::filesystem::path& path,
        const std::array<std::byte, Size>& bytes
    ) {
        std::ofstream file(path, std::ios::binary);

        REQUIRE(file.is_open());

        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );

        REQUIRE(file.good());
    }

    template<std::size_t Size>
    void append_file_bytes(
        const std::filesystem::path& path,
        const std::array<std::byte, Size>& bytes
    ) {
        std::ofstream file(path, std::ios::binary | std::ios::app);

        REQUIRE(file.is_open());

        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );

        REQUIRE(file.good());
    }

    void write_valid_wal_header(
        const std::filesystem::path& path,
        std::uint64_t database_id
    ) {
        std::array<std::byte, WAL_HEADER_SIZE> bytes{};
        const auto header = WalHeader::create_new(database_id);

        REQUIRE(header.encode_into(bytes).ok());

        write_file_bytes(path, bytes);
    }

    Page make_page(PageId page_id, std::uint8_t seed) {
        Page page(page_id);

        for(std::size_t i = 0; i < PAGE_SIZE; i++) {
            page.data()[i] = static_cast<std::byte>((i + seed) % 251);
        }

        return page;
    }

    std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE> make_page_frame_bytes(
        std::uint64_t transaction_id,
        PageId page_id,
        std::uint8_t seed
    ) {
        std::array<std::byte, WAL_PAGE_FRAME_RECORD_SIZE> bytes{};
        const auto page = make_page(page_id, seed);
        const auto frame = WalPageFrame::create_new(
            transaction_id,
            page.id(),
            page.data()
        );

        REQUIRE(frame.encode_into(bytes).ok());

        return bytes;
    }

    std::array<std::byte, WAL_COMMIT_RECORD_SIZE> make_commit_record_bytes(
        std::uint64_t transaction_id,
        std::uint64_t frame_count
    ) {
        std::array<std::byte, WAL_COMMIT_RECORD_SIZE> bytes{};
        const auto commit = WalCommitRecord::create_new(transaction_id, frame_count);

        REQUIRE(commit.encode_into(bytes).ok());

        return bytes;
    }

    std::array<std::byte, sizeof(std::uint32_t)> make_record_type_bytes(
        std::uint32_t record_type
    ) {
        std::array<std::byte, sizeof(std::uint32_t)> bytes{};

        REQUIRE(write_u32_le(bytes, 0, record_type).ok());

        return bytes;
    }

    void append_page_frame(
        const std::filesystem::path& path,
        std::uint64_t transaction_id,
        PageId page_id,
        std::uint8_t seed
    ) {
        const auto bytes = make_page_frame_bytes(transaction_id, page_id, seed);

        append_file_bytes(path, bytes);
    }

    void append_commit(
        const std::filesystem::path& path,
        std::uint64_t transaction_id,
        std::uint64_t frame_count
    ) {
        const auto bytes = make_commit_record_bytes(transaction_id, frame_count);

        append_file_bytes(path, bytes);
    }

    void require_page_offset(
        const WalScanResult& scan_result,
        PageId page_id,
        std::uint64_t expected_offset
    ) {
        const auto it = scan_result.latest_committed_frame_offsets.find(page_id);

        REQUIRE(it != scan_result.latest_committed_frame_offsets.end());
        REQUIRE(it->second == expected_offset);
    }

    void require_page_not_committed(
        const WalScanResult& scan_result,
        PageId page_id
    ) {
        REQUIRE(scan_result.latest_committed_frame_offsets.find(page_id) == scan_result.latest_committed_frame_offsets.end());
    }

    void require_corruption(const std::filesystem::path& path) {
        const auto scan = WalScanner::scan(path, DATABASE_ID);

        REQUIRE_FALSE(scan.ok());
        REQUIRE(scan.status().code() == StatusCode::Corruption);
    }

}

TEST_CASE("WalScanner scans a header-only WAL as empty", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.empty());
    REQUIRE(result.valid_wal_end_offset == WAL_HEADER_SIZE);
    REQUIRE_FALSE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner treats an incomplete tail after the header as ignored bytes", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_file_bytes(path, make_record_type_bytes(WAL_PAGE_FRAME_RECORD_TYPE));

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.empty());
    REQUIRE(result.valid_wal_end_offset == WAL_HEADER_SIZE);
    REQUIRE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner records committed frame offsets and keeps the latest committed frame per page", "[wal][wal-scanner]") {
    constexpr std::uint64_t TX1_PAGE1_OFFSET = WAL_HEADER_SIZE;
    constexpr std::uint64_t TX1_PAGE2_OFFSET = TX1_PAGE1_OFFSET + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t TX1_COMMIT_OFFSET = TX1_PAGE2_OFFSET + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t TX2_PAGE1_OFFSET = TX1_COMMIT_OFFSET + WAL_COMMIT_RECORD_SIZE;
    constexpr std::uint64_t TX2_COMMIT_OFFSET = TX2_PAGE1_OFFSET + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t EXPECTED_END_OFFSET = TX2_COMMIT_OFFSET + WAL_COMMIT_RECORD_SIZE;

    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_2, 2);
    append_commit(path, TRANSACTION_ID_1, 2);
    append_page_frame(path, TRANSACTION_ID_2, PAGE_ID_1, 3);
    append_commit(path, TRANSACTION_ID_2, 1);

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.size() == 2);
    require_page_offset(result, PAGE_ID_1, TX2_PAGE1_OFFSET);
    require_page_offset(result, PAGE_ID_2, TX1_PAGE2_OFFSET);
    REQUIRE(result.valid_wal_end_offset == EXPECTED_END_OFFSET);
    REQUIRE_FALSE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner ignores frames that are never committed", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.empty());
    REQUIRE(result.valid_wal_end_offset == WAL_HEADER_SIZE);
    REQUIRE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner ignores uncommitted frames after the last commit", "[wal][wal-scanner]") {
    constexpr std::uint64_t COMMITTED_PAGE_OFFSET = WAL_HEADER_SIZE;
    constexpr std::uint64_t COMMIT_OFFSET = COMMITTED_PAGE_OFFSET + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t EXPECTED_END_OFFSET = COMMIT_OFFSET + WAL_COMMIT_RECORD_SIZE;

    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);
    append_commit(path, TRANSACTION_ID_1, 1);
    append_page_frame(path, TRANSACTION_ID_2, PAGE_ID_2, 2);

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.size() == 1);
    require_page_offset(result, PAGE_ID_1, COMMITTED_PAGE_OFFSET);
    require_page_not_committed(result, PAGE_ID_2);
    REQUIRE(result.valid_wal_end_offset == EXPECTED_END_OFFSET);
    REQUIRE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner ignores an incomplete trailing record after the last commit", "[wal][wal-scanner]") {
    constexpr std::uint64_t COMMITTED_PAGE_OFFSET = WAL_HEADER_SIZE;
    constexpr std::uint64_t COMMIT_OFFSET = COMMITTED_PAGE_OFFSET + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t EXPECTED_END_OFFSET = COMMIT_OFFSET + WAL_COMMIT_RECORD_SIZE;

    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);
    append_commit(path, TRANSACTION_ID_1, 1);
    append_file_bytes(path, make_record_type_bytes(WAL_PAGE_FRAME_RECORD_TYPE));

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.size() == 1);
    require_page_offset(result, PAGE_ID_1, COMMITTED_PAGE_OFFSET);
    REQUIRE(result.valid_wal_end_offset == EXPECTED_END_OFFSET);
    REQUIRE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner accepts a zero-frame commit as a valid boundary", "[wal][wal-scanner]") {
    constexpr std::uint64_t EXPECTED_END_OFFSET = WAL_HEADER_SIZE + WAL_COMMIT_RECORD_SIZE;

    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_commit(path, TRANSACTION_ID_1, 0);

    auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan.ok());

    const auto& result = scan.value();
    REQUIRE(result.latest_committed_frame_offsets.empty());
    REQUIRE(result.valid_wal_end_offset == EXPECTED_END_OFFSET);
    REQUIRE_FALSE(result.ignored_trailing_bytes);
}

TEST_CASE("WalScanner rejects a WAL for a different database", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, OTHER_DATABASE_ID);

    const auto scan = WalScanner::scan(path, DATABASE_ID);

    REQUIRE_FALSE(scan.ok());
    REQUIRE(scan.status().code() == StatusCode::Corruption);
}

TEST_CASE("WalScanner rejects unknown record types", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_file_bytes(path, make_record_type_bytes(99));

    require_corruption(path);
}

TEST_CASE("WalScanner rejects corrupt frame records", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    auto frame_bytes = make_page_frame_bytes(TRANSACTION_ID_1, PAGE_ID_1, 1);
    frame_bytes[32] = std::byte{ 0xFF };

    write_valid_wal_header(path, DATABASE_ID);
    append_file_bytes(path, frame_bytes);
    append_commit(path, TRANSACTION_ID_1, 1);

    require_corruption(path);
}

TEST_CASE("WalScanner rejects commits with the wrong frame count", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);
    append_commit(path, TRANSACTION_ID_1, 2);

    require_corruption(path);
}

TEST_CASE("WalScanner rejects interleaved transactions", "[wal][wal-scanner]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, DATABASE_ID);
    append_page_frame(path, TRANSACTION_ID_1, PAGE_ID_1, 1);
    append_page_frame(path, TRANSACTION_ID_2, PAGE_ID_2, 2);
    append_commit(path, TRANSACTION_ID_1, 1);

    require_corruption(path);
}
