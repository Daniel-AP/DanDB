#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>

using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;

TEST_CASE("Column create stores column metadata", "[record][column]") {
    const auto column = Column::create(
        "id",
        LogicalType::int64(),
        false,
        true,
        true,
        0,
        1
    );

    REQUIRE(column.ok());
    REQUIRE(column.value().name() == "id");
    REQUIRE(column.value().logical_type().kind() == LogicalType::Kind::Int64);
    REQUIRE_FALSE(column.value().nullable());
    REQUIRE(column.value().pk());
    REQUIRE(column.value().unique());
    REQUIRE(column.value().ordinal() == 0);
    REQUIRE(column.value().fixed_offset() == 1);
}

TEST_CASE("Column create rejects empty names", "[record][column]") {
    const auto column = Column::create(
        "",
        LogicalType::int64(),
        false,
        true,
        true,
        0,
        1
    );

    REQUIRE_FALSE(column.ok());
    REQUIRE(column.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Column create rejects nullable primary keys", "[record][column]") {
    const auto column = Column::create(
        "id",
        LogicalType::int64(),
        true,
        true,
        true,
        0,
        1
    );

    REQUIRE_FALSE(column.ok());
    REQUIRE(column.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Column create rejects nullable unique columns", "[record][column]") {
    const auto column = Column::create(
        "email",
        LogicalType::string(32).value(),
        true,
        false,
        true,
        1,
        9
    );

    REQUIRE_FALSE(column.ok());
    REQUIRE(column.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Column create rejects non-indexable primary key and unique types", "[record][column]") {
    const auto primary_key_column = Column::create(
        "id",
        LogicalType::float64(),
        false,
        true,
        true,
        0,
        1
    );

    const auto unique_column = Column::create(
        "score",
        LogicalType::float64(),
        false,
        false,
        true,
        1,
        9
    );

    REQUIRE_FALSE(primary_key_column.ok());
    REQUIRE(primary_key_column.status().code() == StatusCode::InvalidArgument);

    REQUIRE_FALSE(unique_column.ok());
    REQUIRE(unique_column.status().code() == StatusCode::InvalidArgument);
}
