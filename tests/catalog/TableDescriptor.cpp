#include <catch_amalgamated.hpp>

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableDescriptor.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>

using dandb::catalog::ColumnId;
using dandb::catalog::INVALID_TABLE_ID;
using dandb::catalog::TableDescriptor;
using dandb::catalog::TableId;
using dandb::core::StatusCode;
using dandb::storage::HEADER_PAGE_ID;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

TEST_CASE("TableDescriptor create stores table metadata", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        PageId{ 2 },
        ColumnId{ 3 }
    );

    REQUIRE(descriptor.ok());
    REQUIRE(descriptor.value().table_id() == TableId{ 1 });
    REQUIRE(descriptor.value().name() == "users");
    REQUIRE(descriptor.value().root_page_id() == PageId{ 2 });
    REQUIRE(descriptor.value().primary_key_column_id() == ColumnId{ 3 });
}

TEST_CASE("TableDescriptor create rejects invalid table ids", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        INVALID_TABLE_ID,
        "users",
        PageId{ 2 },
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("TableDescriptor create rejects empty names", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        TableId{ 1 },
        "",
        PageId{ 2 },
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("TableDescriptor create rejects invalid root page ids", "[catalog][table-descriptor]") {
    const auto invalid_root_descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        INVALID_PAGE_ID,
        ColumnId{ 3 }
    );

    const auto header_root_descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        HEADER_PAGE_ID,
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(invalid_root_descriptor.ok());
    REQUIRE(invalid_root_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(header_root_descriptor.ok());
    REQUIRE(header_root_descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("TableDescriptor create rejects invalid primary key column ids", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        PageId{ 2 },
        dandb::catalog::INVALID_COLUMN_ID
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}
