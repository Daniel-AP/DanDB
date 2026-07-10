#include <catch_amalgamated.hpp>

#include <dandb/catalog/ColumnDescriptor.h>
#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Status.h>
#include <dandb/record/LogicalType.h>

using dandb::catalog::ColumnDescriptor;
using dandb::catalog::ColumnId;
using dandb::catalog::INVALID_COLUMN_ID;
using dandb::catalog::INVALID_TABLE_ID;
using dandb::catalog::TableId;
using dandb::core::StatusCode;
using dandb::record::LogicalType;

TEST_CASE("ColumnDescriptor create stores column metadata", "[catalog][column-descriptor]") {
    const auto descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "email",
        LogicalType::string(120).value(),
        2,
        false,
        false,
        true
    );

    REQUIRE(descriptor.ok());
    REQUIRE(descriptor.value().column_id() == ColumnId{ 3 });
    REQUIRE(descriptor.value().table_id() == TableId{ 1 });
    REQUIRE(descriptor.value().name() == "email");
    REQUIRE(descriptor.value().logical_type().kind() == LogicalType::Kind::String);
    REQUIRE(descriptor.value().logical_type().capacity().value() == 120);
    REQUIRE(descriptor.value().ordinal() == 2);
    REQUIRE_FALSE(descriptor.value().nullable());
    REQUIRE_FALSE(descriptor.value().primary_key());
    REQUIRE(descriptor.value().unique());
}

TEST_CASE("ColumnDescriptor create rejects invalid ids", "[catalog][column-descriptor]") {
    const auto invalid_column_descriptor = ColumnDescriptor::create(
        INVALID_COLUMN_ID,
        TableId{ 1 },
        "id",
        LogicalType::int64(),
        0,
        false,
        true,
        true
    );

    const auto invalid_table_descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        INVALID_TABLE_ID,
        "id",
        LogicalType::int64(),
        0,
        false,
        true,
        true
    );

    REQUIRE_FALSE(invalid_column_descriptor.ok());
    REQUIRE(invalid_column_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(invalid_table_descriptor.ok());
    REQUIRE(invalid_table_descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("ColumnDescriptor create rejects empty names", "[catalog][column-descriptor]") {
    const auto descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "",
        LogicalType::int64(),
        0,
        false,
        true,
        true
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("ColumnDescriptor create rejects nullable primary keys", "[catalog][column-descriptor]") {
    const auto descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "id",
        LogicalType::int64(),
        0,
        true,
        true,
        true
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("ColumnDescriptor create rejects non-unique primary keys", "[catalog][column-descriptor]") {
    const auto descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "id",
        LogicalType::int64(),
        0,
        false,
        true,
        false
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("ColumnDescriptor create rejects nullable unique columns", "[catalog][column-descriptor]") {
    const auto descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "email",
        LogicalType::string(120).value(),
        2,
        true,
        false,
        true
    );

    REQUIRE_FALSE(descriptor.ok());
    REQUIRE(descriptor.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("ColumnDescriptor create rejects non-indexable key columns", "[catalog][column-descriptor]") {
    const auto primary_key_descriptor = ColumnDescriptor::create(
        ColumnId{ 3 },
        TableId{ 1 },
        "id",
        LogicalType::float64(),
        0,
        false,
        true,
        true
    );

    const auto unique_descriptor = ColumnDescriptor::create(
        ColumnId{ 4 },
        TableId{ 1 },
        "score",
        LogicalType::float64(),
        1,
        false,
        false,
        true
    );

    REQUIRE_FALSE(primary_key_descriptor.ok());
    REQUIRE(primary_key_descriptor.status().code() == StatusCode::InvalidArgument);
    REQUIRE_FALSE(unique_descriptor.ok());
    REQUIRE(unique_descriptor.status().code() == StatusCode::InvalidArgument);
}
