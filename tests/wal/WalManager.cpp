#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>
#include <dandb/wal/WalCommitRecord.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/wal/WalManager.h>
#include <dandb/wal/WalPageFrame.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

using dandb::core::PAGE_SIZE;
using dandb::core::StatusCode;
using dandb::storage::Page;
using dandb::storage::PageId;
using dandb::wal::WAL_COMMIT_RECORD_SIZE;
using dandb::wal::WAL_HEADER_SIZE;
using dandb::wal::WAL_PAGE_FRAME_RECORD_SIZE;
using dandb::wal::WalCommitRecord;
using dandb::wal::WalHeader;
using dandb::wal::WalManager;
using dandb::wal::WalPageFrame;

namespace {

    constexpr std::uint64_t DATABASE_ID = 0x0102030405060708ULL;
    constexpr std::uint64_t OTHER_DATABASE_ID = 0x1112131415161718ULL;
    constexpr std::uint64_t TRANSACTION_ID = 42;
    constexpr std::uint64_t FRAME_COUNT = 1;
    constexpr PageId PAGE_ID{ 7 };

    template<std::size_t Size>
    std::array<std::byte, Size> read_file_bytes_at(
        const std::filesystem::path& path,
        std::uint64_t offset
    ) {
        std::array<std::byte, Size> bytes{};
        std::ifstream file(path, std::ios::binary);

        REQUIRE(file.is_open());

        file.seekg(static_cast<std::streamoff>(offset));
        REQUIRE(file.good());

        file.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );

        REQUIRE(file.gcount() == static_cast<std::streamsize>(bytes.size()));

        return bytes;
    }

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

    void write_valid_wal_header(
        const std::filesystem::path& path,
        std::uint64_t database_id
    ) {
        std::array<std::byte, WAL_HEADER_SIZE> bytes{};
        const auto header = WalHeader::create_new(database_id);
        const auto status = header.encode_into(bytes);

        REQUIRE(status.ok());

        write_file_bytes(path, bytes);
    }

    void write_one_byte_file(const std::filesystem::path& path) {
        const std::array bytes{ std::byte{ 0x01 } };

        write_file_bytes(path, bytes);
    }

    Page make_page() {
        Page page(PAGE_ID);

        for(std::size_t i = 0; i < PAGE_SIZE; i++) {
            page.data()[i] = static_cast<std::byte>(i % 251);
        }

        return page;
    }

}

TEST_CASE("WalManager open_or_create creates a missing WAL with a valid header", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();

    auto opened = WalManager::open_or_create(path, DATABASE_ID);

    REQUIRE(opened.ok());
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(opened.value().database_id() == DATABASE_ID);

    auto size = opened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == WAL_HEADER_SIZE);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE);

    const auto header_bytes = read_file_bytes_at<WAL_HEADER_SIZE>(path, 0);
    const auto header = WalHeader::decode(header_bytes);

    REQUIRE(header.ok());
    REQUIRE(header.value().database_id() == DATABASE_ID);
}

TEST_CASE("WalManager open_or_create reopens an existing WAL without truncating it", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();

    {
        auto created = WalManager::open_or_create(path, DATABASE_ID);
        REQUIRE(created.ok());

        const auto append_status = created.value().append_commit(TRANSACTION_ID, FRAME_COUNT);
        REQUIRE(append_status.ok());
    }

    auto reopened = WalManager::open_or_create(path, DATABASE_ID);

    REQUIRE(reopened.ok());
    REQUIRE(reopened.value().database_id() == DATABASE_ID);

    auto size = reopened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == WAL_HEADER_SIZE + WAL_COMMIT_RECORD_SIZE);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE + WAL_COMMIT_RECORD_SIZE);
}

TEST_CASE("WalManager open_or_create rejects a WAL for a different database", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_valid_wal_header(path, OTHER_DATABASE_ID);

    auto opened = WalManager::open_or_create(path, DATABASE_ID);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
}

TEST_CASE("WalManager open_or_create rejects a WAL smaller than the header", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    write_one_byte_file(path);

    auto opened = WalManager::open_or_create(path, DATABASE_ID);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
}

TEST_CASE("WalManager open_or_create rejects a corrupt WAL header", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    std::array<std::byte, WAL_HEADER_SIZE> bytes{};

    bytes[0] = std::byte{ 'X' };
    write_file_bytes(path, bytes);

    auto opened = WalManager::open_or_create(path, DATABASE_ID);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
}

TEST_CASE("WalManager append_page_frame writes a frame after the header", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const auto page = make_page();

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto append_status = opened.value().append_page_frame(TRANSACTION_ID, page);

    REQUIRE(append_status.ok());

    auto size = opened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE);

    const auto frame_bytes = read_file_bytes_at<WAL_PAGE_FRAME_RECORD_SIZE>(path, WAL_HEADER_SIZE);
    const auto frame = WalPageFrame::decode(frame_bytes);

    REQUIRE(frame.ok());
    REQUIRE(frame.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(frame.value().page_id() == PAGE_ID);
    REQUIRE(frame.value().page_image() == page.data());
}

TEST_CASE("WalManager append_page_frame rejects an invalid page id", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const Page page;

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto append_status = opened.value().append_page_frame(TRANSACTION_ID, page);

    REQUIRE_FALSE(append_status.ok());
    REQUIRE(append_status.code() == StatusCode::InvalidArgument);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE);
}

TEST_CASE("WalManager append_commit writes a commit after existing frames", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const auto page = make_page();

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto append_frame_status = opened.value().append_page_frame(TRANSACTION_ID, page);
    REQUIRE(append_frame_status.ok());

    const auto append_commit_status = opened.value().append_commit(TRANSACTION_ID, FRAME_COUNT);
    REQUIRE(append_commit_status.ok());

    constexpr std::uint64_t commit_offset = WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE;
    constexpr std::uint64_t expected_size = commit_offset + WAL_COMMIT_RECORD_SIZE;

    auto size = opened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == expected_size);
    REQUIRE(std::filesystem::file_size(path) == expected_size);

    const auto commit_bytes = read_file_bytes_at<WAL_COMMIT_RECORD_SIZE>(path, commit_offset);
    const auto commit = WalCommitRecord::decode(commit_bytes);

    REQUIRE(commit.ok());
    REQUIRE(commit.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(commit.value().frame_count() == FRAME_COUNT);
}

TEST_CASE("WalManager sync succeeds for an open WAL", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto append_status = opened.value().append_commit(TRANSACTION_ID, 0);
    REQUIRE(append_status.ok());

    const auto sync_status = opened.value().sync();

    REQUIRE(sync_status.ok());
}
