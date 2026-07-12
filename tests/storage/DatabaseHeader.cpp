#include <catch_amalgamated.hpp>

#include <dandb/core/Checksum.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/PageId.h>

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
using dandb::storage::DATABASE_FORMAT_VERSION;
using dandb::storage::DATABASE_HEADER_SIZE;
using dandb::storage::DATABASE_MAGIC_BYTES;
using dandb::storage::DatabaseHeader;
using dandb::storage::INITIAL_DATABASE_PAGE_COUNT;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

namespace {

    using PageBytes = std::array<std::byte, PAGE_SIZE>;
    using RootSetter = void (*)(DatabaseHeader&, PageId);

    constexpr std::uint64_t DATABASE_ID = 0x0102030405060708ULL;
    constexpr std::uint64_t PAGE_COUNT_WITH_ROOTS = 6;

    struct RootField {
        const char* name;
        std::size_t offset;
        RootSetter set;
    };

    const std::array<RootField, 4> ROOT_FIELDS{{
        {
            "system tables root",
            32,
            [](DatabaseHeader& header, PageId page_id) { header.set_system_tables_root_page_id(page_id); }
        },
        {
            "system columns root",
            40,
            [](DatabaseHeader& header, PageId page_id) { header.set_system_columns_root_page_id(page_id); }
        },
        {
            "system indexes root",
            48,
            [](DatabaseHeader& header, PageId page_id) { header.set_system_indexes_root_page_id(page_id); }
        },
        {
            "system index columns root",
            56,
            [](DatabaseHeader& header, PageId page_id) { header.set_system_index_columns_root_page_id(page_id); }
        },
    }};

    DatabaseHeader create_header_with_real_roots() {
        auto header = DatabaseHeader::create_new(DATABASE_ID);

        header.set_page_count(PAGE_COUNT_WITH_ROOTS);
        header.set_system_tables_root_page_id({ 1 });
        header.set_system_columns_root_page_id({ 2 });
        header.set_system_indexes_root_page_id({ 3 });
        header.set_system_index_columns_root_page_id({ 4 });

        return header;
    }

    PageBytes encode_header(const DatabaseHeader& header) {
        PageBytes page{};

        const auto status = header.encode_into(page);

        REQUIRE(status.ok());
        return page;
    }

    void rewrite_header_checksum(PageBytes& page) {
        const auto current_checksum = checksum(std::span<const std::byte>(page).first(DATABASE_HEADER_SIZE - sizeof(std::uint64_t)));

        REQUIRE(write_u64_le(page, 120, current_checksum).ok());
    }

    void require_corruption(const PageBytes& page) {
        const auto result = DatabaseHeader::decode(page);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::Corruption);
    }

}

TEST_CASE("create_new initializes an empty database header", "[storage][database-header]") {
    const auto header = DatabaseHeader::create_new(DATABASE_ID);

    REQUIRE(header.database_id() == DATABASE_ID);
    REQUIRE(header.page_count() == INITIAL_DATABASE_PAGE_COUNT);
    REQUIRE(header.system_tables_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(header.system_columns_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(header.system_indexes_root_page_id() == INVALID_PAGE_ID);
    REQUIRE(header.system_index_columns_root_page_id() == INVALID_PAGE_ID);
}

TEST_CASE("database header setters update in-memory fields", "[storage][database-header]") {
    auto header = DatabaseHeader::create_new(DATABASE_ID);

    header.set_page_count(PAGE_COUNT_WITH_ROOTS);
    header.set_system_tables_root_page_id({ 1 });
    header.set_system_columns_root_page_id({ 2 });
    header.set_system_indexes_root_page_id({ 3 });
    header.set_system_index_columns_root_page_id({ 4 });

    REQUIRE(header.page_count() == PAGE_COUNT_WITH_ROOTS);
    REQUIRE(header.system_tables_root_page_id() == PageId{ 1 });
    REQUIRE(header.system_columns_root_page_id() == PageId{ 2 });
    REQUIRE(header.system_indexes_root_page_id() == PageId{ 3 });
    REQUIRE(header.system_index_columns_root_page_id() == PageId{ 4 });
}

TEST_CASE("database header encodes a new header into the documented page zero layout", "[storage][database-header]") {
    PageBytes page{};
    page.fill(std::byte{ 0xAA });

    const auto header = DatabaseHeader::create_new(DATABASE_ID);

    const auto status = header.encode_into(page);

    REQUIRE(status.ok());
    REQUIRE(page[0] == DATABASE_MAGIC_BYTES[0]);
    REQUIRE(page[1] == DATABASE_MAGIC_BYTES[1]);
    REQUIRE(page[2] == DATABASE_MAGIC_BYTES[2]);
    REQUIRE(page[3] == DATABASE_MAGIC_BYTES[3]);

    const auto version = read_u32_le(page, 4);
    REQUIRE(version.ok());
    REQUIRE(version.value() == DATABASE_FORMAT_VERSION);

    const auto page_size = read_u32_le(page, 8);
    REQUIRE(page_size.ok());
    REQUIRE(page_size.value() == PAGE_SIZE);

    const auto header_size = read_u32_le(page, 12);
    REQUIRE(header_size.ok());
    REQUIRE(header_size.value() == DATABASE_HEADER_SIZE);

    const auto database_id = read_u64_le(page, 16);
    REQUIRE(database_id.ok());
    REQUIRE(database_id.value() == DATABASE_ID);

    const auto page_count = read_u64_le(page, 24);
    REQUIRE(page_count.ok());
    REQUIRE(page_count.value() == INITIAL_DATABASE_PAGE_COUNT);

    for(std::size_t offset = 32; offset <= 56; offset += sizeof(std::uint64_t)) {
        const auto page_id = read_u64_le(page, offset);

        REQUIRE(page_id.ok());
        REQUIRE(page_id.value() == INVALID_PAGE_ID.id);
    }

    for(std::size_t offset = 64; offset < 120; offset++) {
        REQUIRE(page[offset] == std::byte{ 0 });
    }

    const auto stored_checksum = read_u64_le(page, 120);
    REQUIRE(stored_checksum.ok());
    REQUIRE(stored_checksum.value() == checksum(std::span<const std::byte>(page).first(DATABASE_HEADER_SIZE - sizeof(std::uint64_t))));

    for(std::size_t offset = DATABASE_HEADER_SIZE; offset < PAGE_SIZE; offset++) {
        REQUIRE(page[offset] == std::byte{ 0 });
    }
}

TEST_CASE("database header encode and decode preserve real root page ids", "[storage][database-header]") {
    const auto page = encode_header(create_header_with_real_roots());

    REQUIRE(read_u64_le(page, 32).value() == 1);
    REQUIRE(read_u64_le(page, 40).value() == 2);
    REQUIRE(read_u64_le(page, 48).value() == 3);
    REQUIRE(read_u64_le(page, 56).value() == 4);

    const auto result = DatabaseHeader::decode(page);

    REQUIRE(result.ok());

    const auto header = result.value();

    REQUIRE(header.database_id() == DATABASE_ID);
    REQUIRE(header.page_count() == PAGE_COUNT_WITH_ROOTS);
    REQUIRE(header.system_tables_root_page_id() == PageId{ 1 });
    REQUIRE(header.system_columns_root_page_id() == PageId{ 2 });
    REQUIRE(header.system_indexes_root_page_id() == PageId{ 3 });
    REQUIRE(header.system_index_columns_root_page_id() == PageId{ 4 });
}

TEST_CASE("database header refuses buffers that are not exactly one page", "[storage][database-header]") {
    SECTION("encode") {
        std::array<std::byte, PAGE_SIZE - 1> page{};
        const auto header = DatabaseHeader::create_new(DATABASE_ID);

        const auto status = header.encode_into(page);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("decode") {
        std::array<std::byte, PAGE_SIZE - 1> page{};

        const auto result = DatabaseHeader::decode(page);

        REQUIRE_FALSE(result.ok());
        REQUIRE(result.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("database header refuses to encode zero page count", "[storage][database-header]") {
    PageBytes page{};
    auto header = DatabaseHeader::create_new(DATABASE_ID);

    header.set_page_count(0);

    const auto status = header.encode_into(page);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);
}

TEST_CASE("database header refuses to encode invalid root page ids", "[storage][database-header]") {
    for(const auto& root_field: ROOT_FIELDS) {
        DYNAMIC_SECTION(root_field.name << " cannot point to page zero") {
            PageBytes page{};
            auto header = DatabaseHeader::create_new(DATABASE_ID);

            header.set_page_count(PAGE_COUNT_WITH_ROOTS);
            root_field.set(header, { 0 });

            const auto status = header.encode_into(page);

            REQUIRE_FALSE(status.ok());
            REQUIRE(status.code() == StatusCode::InvalidArgument);
        }

        DYNAMIC_SECTION(root_field.name << " cannot point past the current page count") {
            PageBytes page{};
            auto header = DatabaseHeader::create_new(DATABASE_ID);

            header.set_page_count(PAGE_COUNT_WITH_ROOTS);
            root_field.set(header, { PAGE_COUNT_WITH_ROOTS });

            const auto status = header.encode_into(page);

            REQUIRE_FALSE(status.ok());
            REQUIRE(status.code() == StatusCode::InvalidArgument);
        }
    }
}

TEST_CASE("database header rejects invalid fixed fields while decoding", "[storage][database-header]") {
    SECTION("bad magic") {
        auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));
        page[0] = std::byte{ 'X' };
        rewrite_header_checksum(page);

        require_corruption(page);
    }

    SECTION("unsupported format version") {
        auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));
        REQUIRE(write_u32_le(page, 4, DATABASE_FORMAT_VERSION + 1).ok());
        rewrite_header_checksum(page);

        require_corruption(page);
    }

    SECTION("unsupported page size") {
        auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));
        REQUIRE(write_u32_le(page, 8, static_cast<std::uint32_t>(PAGE_SIZE * 2)).ok());
        rewrite_header_checksum(page);

        require_corruption(page);
    }

    SECTION("unsupported header size") {
        auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));
        REQUIRE(write_u32_le(page, 12, DATABASE_HEADER_SIZE + 8).ok());
        rewrite_header_checksum(page);

        require_corruption(page);
    }
}

TEST_CASE("database header rejects invalid decoded page count", "[storage][database-header]") {
    auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));

    REQUIRE(write_u64_le(page, 24, 0).ok());
    rewrite_header_checksum(page);

    require_corruption(page);
}

TEST_CASE("database header rejects decoded root page ids outside the database page range", "[storage][database-header]") {
    for(const auto& root_field: ROOT_FIELDS) {
        DYNAMIC_SECTION(root_field.name << " cannot point to page zero") {
            auto page = encode_header(create_header_with_real_roots());

            REQUIRE(write_u64_le(page, root_field.offset, 0).ok());
            rewrite_header_checksum(page);

            require_corruption(page);
        }

        DYNAMIC_SECTION(root_field.name << " cannot point past the current page count") {
            auto page = encode_header(create_header_with_real_roots());

            REQUIRE(write_u64_le(page, root_field.offset, PAGE_COUNT_WITH_ROOTS).ok());
            rewrite_header_checksum(page);

            require_corruption(page);
        }
    }
}

TEST_CASE("database header rejects nonzero reserved bytes", "[storage][database-header]") {
    auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));

    page[64] = std::byte{ 0x01 };
    rewrite_header_checksum(page);

    require_corruption(page);
}

TEST_CASE("database header rejects a bad checksum", "[storage][database-header]") {
    auto page = encode_header(DatabaseHeader::create_new(DATABASE_ID));

    page[16] = std::byte{ 0xFF };

    require_corruption(page);
}
