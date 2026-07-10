#include <catch_amalgamated.hpp>

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/IndexDescriptor.h>
#include <dandb/catalog/IndexId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>

using dandb::catalog::ColumnId;
using dandb::catalog::IndexDescriptor;
using dandb::catalog::IndexId;
using dandb::catalog::INVALID_COLUMN_ID;
using dandb::catalog::INVALID_INDEX_ID;
using dandb::catalog::INVALID_TABLE_ID;
using dandb::catalog::TableId;
using dandb::core::StatusCode;
using dandb::storage::HEADER_PAGE_ID;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

TEST_CASE("IndexDescriptor create stores index metadata", "[catalog][index-descriptor]") {
    const auto descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_email_idx",
        PageId{ 8 },
        true,
        false,
        false,
        ColumnId{ 3 }
    );

    REQUIRE(descriptor.ok());
    REQUIRE(descriptor.value().index_id() == IndexId{ 5 });
    REQUIRE(descriptor.value().table_id() == TableId{ 1 });
    REQUIRE(descriptor.value().name() == "users_email_idx");
    REQUIRE(descriptor.value().root_page_id() == PageId{ 8 });
    REQUIRE(descriptor.value().unique());
    REQUIRE_FALSE(descriptor.value().primary());
    REQUIRE_FALSE(descriptor.value().internal());
    REQUIRE(descriptor.value().indexed_column_id() == ColumnId{ 3 });
}

TEST_CASE("IndexDescriptor create rejects invalid ids", "[catalog][index-descriptor]") {
    const auto invalid_index_descriptor = IndexDescriptor::create(
        INVALID_INDEX_ID,
        TableId{ 1 },
        "users_pk",
        PageId{ 8 },
        true,
        true,
        true,
        ColumnId{ 3 }
    );

    const auto invalid_table_descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        INVALID_TABLE_ID,
        "users_pk",
        PageId{ 8 },
        true,
        true,
        true,
        ColumnId{ 3 }
    );

    const auto invalid_column_descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_pk",
        PageId{ 8 },
        true,
        true,
        true,
        INVALID_COLUMN_ID
    );

    REQUIRE_FALSE(invalid_index_descriptor.ok());
    REQUIRE(invalid_index_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(invalid_table_descriptor.ok());
    REQUIRE(invalid_table_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(invalid_column_descriptor.ok());
    REQUIRE(invalid_column_descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("IndexDescriptor create rejects empty names", "[catalog][index-descriptor]") {
    const auto descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "",
        PageId{ 8 },
        true,
        true,
        true,
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("IndexDescriptor create rejects invalid root page ids", "[catalog][index-descriptor]") {
    const auto invalid_root_descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_pk",
        INVALID_PAGE_ID,
        true,
        true,
        true,
        ColumnId{ 3 }
    );

    const auto header_root_descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_pk",
        HEADER_PAGE_ID,
        true,
        true,
        true,
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(invalid_root_descriptor.ok());
    REQUIRE(invalid_root_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(header_root_descriptor.ok());
    REQUIRE(header_root_descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("IndexDescriptor create rejects primary indexes that are not unique", "[catalog][index-descriptor]") {
    const auto descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_pk",
        PageId{ 8 },
        false,
        true,
        true,
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("IndexDescriptor create rejects primary indexes that are not internal", "[catalog][index-descriptor]") {
    const auto descriptor = IndexDescriptor::create(
        IndexId{ 5 },
        TableId{ 1 },
        "users_pk",
        PageId{ 8 },
        true,
        true,
        false,
        ColumnId{ 3 }
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}
