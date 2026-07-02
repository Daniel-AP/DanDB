#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/platform/FileFaultInjector.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>
#include <dandb/wal/WalCommitRecord.h>
#include <dandb/wal/WalHeader.h>
#include <dandb/wal/WalManager.h>
#include <dandb/wal/WalPageFrame.h>
#include <dandb/wal/WalScanner.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

    class RecordingFaultInjector final : public dandb::platform::FileFaultInjector {
        public:
            std::vector<std::string> events;
            bool fail_sync = false;

            dandb::core::Status before_write(
                std::uint64_t offset,
                std::size_t byte_count
            ) override {
                if(offset == WAL_HEADER_SIZE && byte_count == WAL_PAGE_FRAME_RECORD_SIZE) {
                    events.push_back("write_page_frame");
                } else if(
                    offset == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE &&
                    byte_count == WAL_COMMIT_RECORD_SIZE
                ) {
                    events.push_back("write_commit");
                } else {
                    events.push_back("write");
                }

                return dandb::core::Status::Ok();
            }

            dandb::core::Status before_sync() override {
                events.push_back("sync");

                if(fail_sync) {
                    return dandb::core::Status::IoError("injected sync failure");
                }

                return dandb::core::Status::Ok();
            }
    };

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

        const std::array<Page, 0> pages{};
        const auto commit_status = created.value().commit_transaction(TRANSACTION_ID, pages);
        REQUIRE(commit_status.ok());
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

TEST_CASE("WalManager commit_transaction writes page frames after the header", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE(commit_status.ok());

    auto size = opened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE + WAL_COMMIT_RECORD_SIZE);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE + WAL_COMMIT_RECORD_SIZE);

    const auto frame_bytes = read_file_bytes_at<WAL_PAGE_FRAME_RECORD_SIZE>(path, WAL_HEADER_SIZE);
    const auto frame = WalPageFrame::decode(frame_bytes);

    REQUIRE(frame.ok());
    REQUIRE(frame.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(frame.value().page_id() == PAGE_ID);
    REQUIRE(frame.value().page_image() == pages[0].data());
}

TEST_CASE("WalManager commit_transaction rejects an invalid page id", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ Page{} };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE_FALSE(commit_status.ok());
    REQUIRE(commit_status.code() == StatusCode::InvalidArgument);
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE);
}

TEST_CASE("WalManager commit_transaction writes a commit after page frames", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);
    REQUIRE(commit_status.ok());

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

TEST_CASE("WalManager commit_transaction writes a zero-frame commit", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array<Page, 0> pages{};

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE(commit_status.ok());

    auto size = opened.value().size();
    REQUIRE(size.ok());
    REQUIRE(size.value() == WAL_HEADER_SIZE + WAL_COMMIT_RECORD_SIZE);

    const auto commit_bytes = read_file_bytes_at<WAL_COMMIT_RECORD_SIZE>(path, WAL_HEADER_SIZE);
    const auto commit = WalCommitRecord::decode(commit_bytes);

    REQUIRE(commit.ok());
    REQUIRE(commit.value().transaction_id() == TRANSACTION_ID);
    REQUIRE(commit.value().frame_count() == 0);
}

TEST_CASE("WalManager commit_transaction syncs after writing frames and commit record", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    RecordingFaultInjector injector;
    opened.value().set_fault_injector(&injector);

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE(commit_status.ok());

    const std::vector<std::string> expected_events{
        "write_page_frame",
        "write_commit",
        "sync"
    };
    REQUIRE(injector.events == expected_events);
}

TEST_CASE("WalManager commit_transaction returns injected sync failure", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    RecordingFaultInjector injector;
    injector.fail_sync = true;
    opened.value().set_fault_injector(&injector);

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE_FALSE(commit_status.ok());
    REQUIRE(commit_status.code() == StatusCode::IoError);

    const std::vector<std::string> expected_events{
        "write_page_frame",
        "write_commit",
        "sync"
    };
    REQUIRE(injector.events == expected_events);
}

TEST_CASE("WalManager commit_transaction leaves scanable WAL bytes when sync fails", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    RecordingFaultInjector injector;
    injector.fail_sync = true;
    opened.value().set_fault_injector(&injector);

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);

    REQUIRE_FALSE(commit_status.ok());
    REQUIRE(commit_status.code() == StatusCode::IoError);

    auto scan_result = dandb::wal::WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan_result.ok());
    REQUIRE(scan_result.value().latest_committed_frame_offsets.at(PAGE_ID) == WAL_HEADER_SIZE);
    REQUIRE(scan_result.value().valid_wal_end_offset == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE + WAL_COMMIT_RECORD_SIZE);
    REQUIRE_FALSE(scan_result.value().ignored_trailing_bytes);
}

TEST_CASE("WalManager reset produces a valid empty WAL after appended records", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);
    REQUIRE(commit_status.ok());

    const auto reset_status = opened.value().reset();

    REQUIRE(reset_status.ok());
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE);

    const auto header_bytes = read_file_bytes_at<WAL_HEADER_SIZE>(path, 0);
    const auto header = WalHeader::decode(header_bytes);

    REQUIRE(header.ok());
    REQUIRE(header.value().database_id() == DATABASE_ID);

    auto scan_result = dandb::wal::WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan_result.ok());
    REQUIRE(scan_result.value().latest_committed_frame_offsets.empty());
    REQUIRE(scan_result.value().valid_wal_end_offset == WAL_HEADER_SIZE);
    REQUIRE_FALSE(scan_result.value().ignored_trailing_bytes);
}

TEST_CASE("WalManager can append records after reset", "[wal][wal-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.wal_path();
    const std::array pages{ make_page() };

    auto opened = WalManager::open_or_create(path, DATABASE_ID);
    REQUIRE(opened.ok());

    const auto first_commit_status = opened.value().commit_transaction(TRANSACTION_ID, pages);
    REQUIRE(first_commit_status.ok());

    const auto reset_status = opened.value().reset();
    REQUIRE(reset_status.ok());

    const auto second_commit_status = opened.value().commit_transaction(TRANSACTION_ID + 1, pages);

    REQUIRE(second_commit_status.ok());
    REQUIRE(std::filesystem::file_size(path) == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE + WAL_COMMIT_RECORD_SIZE);

    auto scan_result = dandb::wal::WalScanner::scan(path, DATABASE_ID);

    REQUIRE(scan_result.ok());
    REQUIRE(scan_result.value().latest_committed_frame_offsets.size() == 1);
    REQUIRE(scan_result.value().latest_committed_frame_offsets.at(PAGE_ID) == WAL_HEADER_SIZE);
    REQUIRE(scan_result.value().valid_wal_end_offset == WAL_HEADER_SIZE + WAL_PAGE_FRAME_RECORD_SIZE + WAL_COMMIT_RECORD_SIZE);
    REQUIRE_FALSE(scan_result.value().ignored_trailing_bytes);
}
