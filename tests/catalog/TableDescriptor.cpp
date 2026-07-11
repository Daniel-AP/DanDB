#include <catch_amalgamated.hpp>

#include <dandb/catalog/TableDescriptor.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>

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
        PageId{ 2 }
    );

    REQUIRE(descriptor.ok());
    REQUIRE(descriptor.value().table_id() == TableId{ 1 });
    REQUIRE(descriptor.value().name() == "users");
    REQUIRE(descriptor.value().root_page_id() == PageId{ 2 });
}

TEST_CASE("TableDescriptor create rejects invalid table ids", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        INVALID_TABLE_ID,
        "users",
        PageId{ 2 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("TableDescriptor create rejects empty names", "[catalog][table-descriptor]") {
    const auto descriptor = TableDescriptor::create(
        TableId{ 1 },
        "",
        PageId{ 2 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("TableDescriptor create rejects invalid root page ids", "[catalog][table-descriptor]") {
    const auto invalid_root_descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        INVALID_PAGE_ID
    );

    const auto header_root_descriptor = TableDescriptor::create(
        TableId{ 1 },
        "users",
        HEADER_PAGE_ID
    );

    REQUIRE_FALSE(invalid_root_descriptor.ok());
    REQUIRE(invalid_root_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(header_root_descriptor.ok());
    REQUIRE(header_root_descriptor.status().code() == StatusCode::InvalidArgument);
}
