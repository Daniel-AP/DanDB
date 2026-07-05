#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Schema.h>

#include <utility>
#include <vector>

using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;
using dandb::record::Schema;

namespace {

    Column make_column(
        std::string name,
        LogicalType logical_type,
        bool nullable,
        bool pk,
        bool unique
    ) {
        auto column = Column::create(
            std::move(name),
            logical_type,
            nullable,
            pk,
            unique
        );

        REQUIRE(column.ok());
        return std::move(column.value());
    }

}

TEST_CASE("Schema create computes deterministic fixed row layout", "[record][schema]") {
    auto name_type = LogicalType::string(8);

    REQUIRE(name_type.ok());

    auto schema = Schema::create({
        make_column("id", LogicalType::int64(), false, true, true),
        make_column("name", name_type.value(), true, false, false),
        make_column("active", LogicalType::boolean(), false, false, false)
    });

    REQUIRE(schema.ok());
    REQUIRE(schema.value().column_count() == 3);
    REQUIRE(schema.value().null_bitmap_size() == 1);
    REQUIRE(schema.value().row_size() == 18);
    REQUIRE(schema.value().primary_key_ordinal() == 0);
    REQUIRE(schema.value().primary_key_column().name() == "id");

    REQUIRE(schema.value().column(0).ordinal() == 0);
    REQUIRE(schema.value().column(0).fixed_offset() == 1);

    REQUIRE(schema.value().column(1).ordinal() == 1);
    REQUIRE(schema.value().column(1).fixed_offset() == 9);

    REQUIRE(schema.value().column(2).ordinal() == 2);
    REQUIRE(schema.value().column(2).fixed_offset() == 17);
}

TEST_CASE("Schema create rejects empty column lists", "[record][schema]") {
    auto schema = Schema::create({});

    REQUIRE_FALSE(schema.ok());
    REQUIRE(schema.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Schema create rejects schemas without a primary key", "[record][schema]") {
    auto schema = Schema::create({
        make_column("id", LogicalType::int64(), false, false, true)
    });

    REQUIRE_FALSE(schema.ok());
    REQUIRE(schema.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Schema create rejects schemas with more than one primary key", "[record][schema]") {
    auto schema = Schema::create({
        make_column("id", LogicalType::int64(), false, true, true),
        make_column("account_id", LogicalType::int64(), false, true, true)
    });

    REQUIRE_FALSE(schema.ok());
    REQUIRE(schema.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("Schema create rejects duplicate column names", "[record][schema]") {
    auto schema = Schema::create({
        make_column("id", LogicalType::int64(), false, true, true),
        make_column("id", LogicalType::boolean(), false, false, false)
    });

    REQUIRE_FALSE(schema.ok());
    REQUIRE(schema.status().code() == StatusCode::InvalidArgument);
}
