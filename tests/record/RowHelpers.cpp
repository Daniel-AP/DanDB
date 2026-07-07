#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Row.h>
#include <dandb/record/RowHelpers.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;
using dandb::record::Row;
using dandb::record::RowHelpers;
using dandb::record::Schema;
using dandb::record::Value;

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

    Schema make_schema() {
        auto name_type = LogicalType::string(8);
        REQUIRE(name_type.ok());

        auto schema = Schema::create({
            make_column("id", LogicalType::int64(), false, true, true),
            make_column("name", name_type.value(), true, false, false),
            make_column("active", LogicalType::boolean(), false, false, false)
        });

        REQUIRE(schema.ok());
        return std::move(schema.value());
    }

    Schema make_float_schema() {
        auto schema = Schema::create({
            make_column("id", LogicalType::int64(), false, true, true),
            make_column("score", LogicalType::float64(), false, false, false)
        });

        REQUIRE(schema.ok());
        return std::move(schema.value());
    }

    Row make_row() {
        auto name = Value::string("dan", 8);
        REQUIRE(name.ok());

        return Row(std::vector<Value>{
            Value::int64(7),
            name.value(),
            Value::boolean(true)
        });
    }

}

TEST_CASE("RowHelpers builds a row from ordered values that match the schema", "[record][row-helpers]") {
    auto schema = make_schema();
    auto name = Value::string("dan", 8);
    REQUIRE(name.ok());

    auto row = RowHelpers::build_row(schema, {
        Value::int64(7),
        name.value(),
        Value::boolean(true)
    });

    REQUIRE(row.ok());
    REQUIRE(row.value().value_count() == 3);
    REQUIRE(row.value().value(0).as_integer() == 7);
    REQUIRE(row.value().value(1).as_string() == "dan");
    REQUIRE(row.value().value(2).as_boolean());
}

TEST_CASE("RowHelpers rejects rows that do not match the schema", "[record][row-helpers]") {
    auto schema = make_schema();

    SECTION("wrong value count") {
        auto row = RowHelpers::build_row(schema, {
            Value::int64(7)
        });

        REQUIRE_FALSE(row.ok());
        REQUIRE(row.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("wrong value type") {
        auto row = RowHelpers::build_row(schema, {
            Value::int64(7),
            Value::boolean(false),
            Value::boolean(true)
        });

        REQUIRE_FALSE(row.ok());
        REQUIRE(row.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("string capacity does not match schema") {
        auto name = Value::string("dan", 4);
        REQUIRE(name.ok());

        auto row = RowHelpers::build_row(schema, {
            Value::int64(7),
            name.value(),
            Value::boolean(true)
        });

        REQUIRE_FALSE(row.ok());
        REQUIRE(row.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("null in non-nullable column") {
        auto row = RowHelpers::build_row(schema, {
            Value::int64(7),
            Value::null(schema.column(1).logical_type()),
            Value::null(schema.column(2).logical_type())
        });

        REQUIRE_FALSE(row.ok());
        REQUIRE(row.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("extra value") {
        auto name = Value::string("dan", 8);
        REQUIRE(name.ok());

        auto row = RowHelpers::build_row(schema, {
            Value::int64(7),
            name.value(),
            Value::boolean(true),
            Value::int64(99)
        });

        REQUIRE_FALSE(row.ok());
        REQUIRE(row.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("RowHelpers gets values by ordinal", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto id = RowHelpers::value_by_ordinal(schema, row, 0);
    auto name = RowHelpers::value_by_ordinal(schema, row, 1);
    auto active = RowHelpers::value_by_ordinal(schema, row, 2);

    REQUIRE(id.ok());
    REQUIRE(id.value().as_integer() == 7);

    REQUIRE(name.ok());
    REQUIRE(name.value().as_string() == "dan");

    REQUIRE(active.ok());
    REQUIRE(active.value().as_boolean());
}

TEST_CASE("RowHelpers rejects ordinal lookup outside the schema", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto value = RowHelpers::value_by_ordinal(schema, row, 3);

    REQUIRE_FALSE(value.ok());
    REQUIRE(value.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("RowHelpers gets values by column name", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto name = RowHelpers::value_by_name(schema, row, "name");

    REQUIRE(name.ok());
    REQUIRE(name.value().as_string() == "dan");
}

TEST_CASE("RowHelpers returns not found for a missing column name", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto value = RowHelpers::value_by_name(schema, row, "missing");

    REQUIRE_FALSE(value.ok());
    REQUIRE(value.status().code() == StatusCode::NotFound);
}

TEST_CASE("RowHelpers rejects value lookup when the row does not match the schema", "[record][row-helpers]") {
    auto schema = make_schema();

    Row row(std::vector<Value>{
        Value::int64(7),
        Value::boolean(false),
        Value::boolean(true)
    });

    SECTION("by ordinal") {
        auto value = RowHelpers::value_by_ordinal(schema, row, 1);

        REQUIRE_FALSE(value.ok());
        REQUIRE(value.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("by name") {
        auto value = RowHelpers::value_by_name(schema, row, "name");

        REQUIRE_FALSE(value.ok());
        REQUIRE(value.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("RowHelpers replaces non-primary-key values", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();
    auto name = Value::string("ana", 8);
    REQUIRE(name.ok());

    auto replaced = RowHelpers::replace_non_primary_key_values(
        schema,
        row,
        { 1, 2 },
        { name.value(), Value::boolean(false) }
    );

    REQUIRE(replaced.ok());
    REQUIRE(replaced.value().value(0).as_integer() == 7);
    REQUIRE(replaced.value().value(1).as_string() == "ana");
    REQUIRE_FALSE(replaced.value().value(2).as_boolean());
}

TEST_CASE("RowHelpers rejects invalid field replacement", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    SECTION("primary key update") {
        auto replaced = RowHelpers::replace_non_primary_key_values(
            schema,
            row,
            { 0 },
            { Value::int64(8) }
        );

        REQUIRE_FALSE(replaced.ok());
        REQUIRE(replaced.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("different ordinal and value counts") {
        auto replaced = RowHelpers::replace_non_primary_key_values(
            schema,
            row,
            { 1, 2 },
            { Value::boolean(false) }
        );

        REQUIRE_FALSE(replaced.ok());
        REQUIRE(replaced.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("ordinal outside the schema") {
        auto replaced = RowHelpers::replace_non_primary_key_values(
            schema,
            row,
            { 3 },
            { Value::boolean(false) }
        );

        REQUIRE_FALSE(replaced.ok());
        REQUIRE(replaced.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("replacement value does not match column type") {
        auto replaced = RowHelpers::replace_non_primary_key_values(
            schema,
            row,
            { 1 },
            { Value::boolean(false) }
        );

        REQUIRE_FALSE(replaced.ok());
        REQUIRE(replaced.status().code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("RowHelpers extracts primary key bytes", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto key = RowHelpers::primary_key(schema, row);

    REQUIRE(key.ok());
    REQUIRE(key.value() == std::vector<std::byte>{
        std::byte{ 0x80 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x07 }
    });
}

TEST_CASE("RowHelpers encodes an indexed key from a selected column", "[record][row-helpers]") {
    auto schema = make_schema();
    auto row = make_row();

    auto key = RowHelpers::indexed_key(schema, row, 1);

    REQUIRE(key.ok());
    REQUIRE(key.value() == std::vector<std::byte>{
        std::byte{ 0x64 },
        std::byte{ 0x61 },
        std::byte{ 0x6E },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 },
        std::byte{ 0x00 }
    });
}

TEST_CASE("RowHelpers rejects invalid indexed keys", "[record][row-helpers]") {
    SECTION("ordinal outside the schema") {
        auto schema = make_schema();
        auto row = make_row();

        auto key = RowHelpers::indexed_key(schema, row, 3);

        REQUIRE_FALSE(key.ok());
        REQUIRE(key.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("null key value") {
        auto schema = make_schema();

        Row row(std::vector<Value>{
            Value::int64(7),
            Value::null(schema.column(1).logical_type()),
            Value::boolean(true)
        });

        auto key = RowHelpers::indexed_key(schema, row, 1);

        REQUIRE_FALSE(key.ok());
        REQUIRE(key.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("non-indexable value type") {
        auto schema = make_float_schema();

        Row row(std::vector<Value>{
            Value::int64(7),
            Value::float64(1.5)
        });

        auto key = RowHelpers::indexed_key(schema, row, 1);

        REQUIRE_FALSE(key.ok());
        REQUIRE(key.status().code() == StatusCode::InvalidArgument);
    }
}
