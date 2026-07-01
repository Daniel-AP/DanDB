#include <catch_amalgamated.hpp>

#include <testutil/TempDir.h>
#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/DiskManager.h>
#include <dandb/storage/PageId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

using dandb::core::PAGE_SIZE;
using dandb::core::StatusCode;
using dandb::core::checksum;
using dandb::core::write_u32_le;
using dandb::core::write_u64_le;
using dandb::storage::DATABASE_FORMAT_VERSION;
using dandb::storage::DATABASE_HEADER_SIZE;
using dandb::storage::DatabaseHeader;
using dandb::storage::DiskManager;
using dandb::storage::HEADER_PAGE_ID;
using dandb::storage::INITIAL_DATABASE_PAGE_COUNT;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

namespace {

    using PageBytes = std::array<std::byte, PAGE_SIZE>;

    constexpr std::uint64_t DATABASE_ID = 0x0102030405060708ULL;
    constexpr std::size_t DATABASE_HEADER_CHECKSUM_OFFSET = 120;

    PageBytes encode_header_page(const DatabaseHeader& header) {
        PageBytes page{};

        const auto status = header.encode_into(page);

        REQUIRE(status.ok());
        return page;
    }

    void rewrite_header_checksum(PageBytes& page) {
        const auto current_checksum = checksum(
            std::span<const std::byte>(page).first(DATABASE_HEADER_SIZE - sizeof(std::uint64_t))
        );

        REQUIRE(write_u64_le(page, DATABASE_HEADER_CHECKSUM_OFFSET, current_checksum).ok());
    }

    void write_file_page(const std::filesystem::path& path, const PageBytes& page) {
        std::ofstream file(path, std::ios::binary);

        REQUIRE(file.is_open());

        file.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));

        REQUIRE(file.good());
    }

}

TEST_CASE("DiskManager creates a database file and reopens its header", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "created.ddb";
    const auto initial_header = DatabaseHeader::create_new(DATABASE_ID);

    auto created = DiskManager::create_new(path, initial_header);

    REQUIRE(created.ok());
    REQUIRE(std::filesystem::file_size(path) == PAGE_SIZE);

    auto created_size = created.value().size();
    REQUIRE(created_size.ok());
    REQUIRE(created_size.value() == PAGE_SIZE);

    auto opened = DiskManager::open_existing(path);

    REQUIRE(opened.ok());

    auto reopened_header = opened.value().read_header();

    REQUIRE(reopened_header.ok());
    REQUIRE(reopened_header.value().database_id() == DATABASE_ID);
    REQUIRE(reopened_header.value().page_count() == INITIAL_DATABASE_PAGE_COUNT);
    REQUIRE(reopened_header.value().catalog_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(reopened_header.value().system_tables_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(reopened_header.value().system_columns_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(reopened_header.value().system_indexes_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(reopened_header.value().system_index_columns_root_page_id() == INVALID_PAGE_ID);
}

TEST_CASE("DiskManager persists an updated database header page count", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "updated_page_count.ddb";

    {
        const auto initial_header = DatabaseHeader::create_new(DATABASE_ID);

        auto created = DiskManager::create_new(path, initial_header);
        REQUIRE(created.ok());

        auto header_result = created.value().read_header();
        REQUIRE(header_result.ok());

        auto header = header_result.value();
        header.set_page_count(2);

        const auto write_status = created.value().write_header(header);
        REQUIRE(write_status.ok());
    }

    std::filesystem::resize_file(path, PAGE_SIZE * 2);

    auto opened = DiskManager::open_existing(path);
    REQUIRE(opened.ok());

    auto reopened_header = opened.value().read_header();
    REQUIRE(reopened_header.ok());
    REQUIRE(reopened_header.value().database_id() == DATABASE_ID);
    REQUIRE(reopened_header.value().page_count() == 2);
}

TEST_CASE("DiskManager open_existing rejects header page count that does not match file size", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "mismatched_page_count.ddb";
    auto initial_header = DatabaseHeader::create_new(DATABASE_ID);
    initial_header.set_page_count(2);

    {
        auto created = DiskManager::create_new(path, initial_header);
        REQUIRE(created.ok());
    }

    std::filesystem::resize_file(path, PAGE_SIZE);

    auto opened = DiskManager::open_existing(path);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
    REQUIRE_FALSE(opened.status().message().empty());
}

TEST_CASE("DiskManager persists an updated database header catalog root page id", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "updated_catalog_root.ddb";
    auto initial_header = DatabaseHeader::create_new(DATABASE_ID);
    initial_header.set_page_count(2);

    auto created = DiskManager::create_new(path, initial_header);
    REQUIRE(created.ok());

    auto header_result = created.value().read_header();
    REQUIRE(header_result.ok());

    auto header = header_result.value();
    header.set_catalog_root_page_id(PageId{ 1 });

    const auto write_status = created.value().write_header(header);
    REQUIRE(write_status.ok());

    auto opened = DiskManager::open_existing(path);
    REQUIRE(opened.ok());

    auto reopened_header = opened.value().read_header();
    REQUIRE(reopened_header.ok());
    REQUIRE(reopened_header.value().database_id() == DATABASE_ID);
    REQUIRE(reopened_header.value().page_count() == 2);
    REQUIRE(reopened_header.value().catalog_root_page_id() == PageId{ 1 });
}

TEST_CASE("DiskManager open_existing fails for an empty file", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "empty.ddb";

    {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
    }

    auto opened = DiskManager::open_existing(path);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::IoError);
}

TEST_CASE("DiskManager open_existing fails for a bad header", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "bad_header.ddb";

    PageBytes bad_header_page{};
    bad_header_page[0] = std::byte{ 'X' };
    write_file_page(path, bad_header_page);

    auto opened = DiskManager::open_existing(path);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
}

TEST_CASE("DiskManager open_existing rejects manually corrupted database header fields", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "corrupt_header_field.ddb";
    auto header_page = encode_header_page(DatabaseHeader::create_new(DATABASE_ID));

    SECTION("magic") {
        header_page[0] = std::byte{ 'X' };
        rewrite_header_checksum(header_page);
    }

    SECTION("version") {
        REQUIRE(write_u32_le(header_page, 4, DATABASE_FORMAT_VERSION + 1).ok());
        rewrite_header_checksum(header_page);
    }

    SECTION("page size") {
        REQUIRE(write_u32_le(header_page, 8, static_cast<std::uint32_t>(PAGE_SIZE * 2)).ok());
        rewrite_header_checksum(header_page);
    }

    SECTION("checksum") {
        header_page[16] = std::byte{ 0xFF };
    }

    write_file_page(path, header_page);

    auto opened = DiskManager::open_existing(path);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
    REQUIRE_FALSE(opened.status().message().empty());
}

TEST_CASE("DiskManager open_existing rejects a file whose size is not a whole number of pages", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "partial_page.ddb";
    const auto initial_header = DatabaseHeader::create_new(DATABASE_ID);

    auto created = DiskManager::create_new(path, initial_header);
    REQUIRE(created.ok());

    {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        REQUIRE(file.is_open());

        const char extra_byte = '\0';
        file.write(&extra_byte, 1);

        REQUIRE(file.good());
    }

    auto opened = DiskManager::open_existing(path);

    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status().code() == StatusCode::Corruption);
    REQUIRE_FALSE(opened.status().message().empty());
}

TEST_CASE("DiskManager writes and reads a non-header page", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "pages.ddb";
    auto initial_header = DatabaseHeader::create_new(DATABASE_ID);
    initial_header.set_page_count(2);

    auto created = DiskManager::create_new(path, initial_header);
    REQUIRE(created.ok());
    REQUIRE(std::filesystem::file_size(path) == PAGE_SIZE * 2);

    PageBytes page_bytes{};
    page_bytes[0] = std::byte{ 0x12 };
    page_bytes[PAGE_SIZE - 1] = std::byte{ 0x34 };

    const auto write_status = created.value().write_page(PageId{ 1 }, page_bytes);

    REQUIRE(write_status.ok());

    auto read_page = created.value().read_page(PageId{ 1 });

    REQUIRE(read_page.ok());
    REQUIRE(read_page.value().id() == PageId{ 1 });
    REQUIRE(read_page.value().data()[0] == std::byte{ 0x12 });
    REQUIRE(read_page.value().data()[PAGE_SIZE - 1] == std::byte{ 0x34 });
}

TEST_CASE("DiskManager keeps page zero reserved for header methods", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "header_page.ddb";
    const auto initial_header = DatabaseHeader::create_new(DATABASE_ID);

    auto created = DiskManager::create_new(path, initial_header);
    REQUIRE(created.ok());

    PageBytes page_bytes{};

    auto read_page = created.value().read_page(HEADER_PAGE_ID);
    const auto write_status = created.value().write_page(HEADER_PAGE_ID, page_bytes);

    REQUIRE_FALSE(read_page.ok());
    REQUIRE(read_page.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(write_status.ok());
    REQUIRE(write_status.code() == StatusCode::InvalidArgument);
}

TEST_CASE("DiskManager rejects page access outside the file", "[storage][disk-manager]") {
    const dandb::testutil::TempDir temp_dir;
    const auto path = temp_dir.path() / "outside_file.ddb";
    const auto initial_header = DatabaseHeader::create_new(DATABASE_ID);

    auto created = DiskManager::create_new(path, initial_header);
    REQUIRE(created.ok());

    PageBytes page_bytes{};

    auto read_page = created.value().read_page(PageId{ 1 });
    const auto write_status = created.value().write_page(PageId{ 1 }, page_bytes);

    REQUIRE_FALSE(read_page.ok());
    REQUIRE(read_page.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(write_status.ok());
    REQUIRE(write_status.code() == StatusCode::InvalidArgument);
    REQUIRE(std::filesystem::file_size(path) == PAGE_SIZE);
}
