#include <catch_amalgamated.hpp>

#include <dandb/catalog/SystemTables.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Schema.h>

#include <cstddef>
#include <optional>
#include <string_view>

using dandb::catalog::CATALOG_NAME_CAPACITY;
using dandb::catalog::DANDB_COLUMNS_NAME;
using dandb::catalog::DANDB_INDEXES_NAME;
using dandb::catalog::DANDB_INDEX_COLUMNS_NAME;
using dandb::catalog::DANDB_TABLES_NAME;
using dandb::catalog::SystemTables;
using dandb::record::LogicalType;
using dandb::record::Schema;

namespace {

    void require_column(
        const Schema& schema,
        std::size_t ordinal,
        std::string_view name,
        LogicalType::Kind kind,
        bool nullable,
        bool pk,
        bool unique,
        std::optional<std::size_t> capacity = std::nullopt
    ) {
        const auto& column = schema.column(ordinal);

        REQUIRE(column.ordinal() == ordinal);
        REQUIRE(column.name() == name);
        REQUIRE(column.logical_type().kind() == kind);
        REQUIRE(column.logical_type().capacity() == capacity);
        REQUIRE(column.nullable() == nullable);
        REQUIRE(column.pk() == pk);
        REQUIRE(column.unique() == unique);
    }

}

TEST_CASE("SystemTables exposes system table names", "[catalog][system-tables]") {
    REQUIRE(std::string_view{ DANDB_TABLES_NAME } == "dandb_tables");
    REQUIRE(std::string_view{ DANDB_COLUMNS_NAME } == "dandb_columns");
    REQUIRE(std::string_view{ DANDB_INDEXES_NAME } == "dandb_indexes");
    REQUIRE(std::string_view{ DANDB_INDEX_COLUMNS_NAME } == "dandb_index_columns");
}

TEST_CASE("SystemTables defines dandb_tables schema", "[catalog][system-tables]") {
    auto schema = SystemTables::tables_schema();

    REQUIRE(schema.ok());
    REQUIRE(schema.value().column_count() == 4);
    REQUIRE(schema.value().primary_key_ordinal() == 0);
    REQUIRE(schema.value().primary_key_column().name() == "table_id");

    require_column(schema.value(), 0, "table_id", LogicalType::Kind::Int64, false, true, true);
    require_column(schema.value(), 1, "name", LogicalType::Kind::String, false, false, true, CATALOG_NAME_CAPACITY);
    require_column(schema.value(), 2, "root_page_id", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 3, "primary_key_column_id", LogicalType::Kind::Int64, false, false, false);
}

TEST_CASE("SystemTables defines dandb_columns schema", "[catalog][system-tables]") {
    auto schema = SystemTables::columns_schema();

    REQUIRE(schema.ok());
    REQUIRE(schema.value().column_count() == 9);
    REQUIRE(schema.value().primary_key_ordinal() == 0);
    REQUIRE(schema.value().primary_key_column().name() == "column_id");

    require_column(schema.value(), 0, "column_id", LogicalType::Kind::Int64, false, true, true);
    require_column(schema.value(), 1, "table_id", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 2, "name", LogicalType::Kind::String, false, false, false, CATALOG_NAME_CAPACITY);
    require_column(schema.value(), 3, "type_kind", LogicalType::Kind::Int8, false, false, false);
    require_column(schema.value(), 4, "type_capacity", LogicalType::Kind::Int64, true, false, false);
    require_column(schema.value(), 5, "ordinal", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 6, "nullable", LogicalType::Kind::Boolean, false, false, false);
    require_column(schema.value(), 7, "primary_key", LogicalType::Kind::Boolean, false, false, false);
    require_column(schema.value(), 8, "unique", LogicalType::Kind::Boolean, false, false, false);
}

TEST_CASE("SystemTables defines dandb_indexes schema", "[catalog][system-tables]") {
    auto schema = SystemTables::indexes_schema();

    REQUIRE(schema.ok());
    REQUIRE(schema.value().column_count() == 7);
    REQUIRE(schema.value().primary_key_ordinal() == 0);
    REQUIRE(schema.value().primary_key_column().name() == "index_id");

    require_column(schema.value(), 0, "index_id", LogicalType::Kind::Int64, false, true, true);
    require_column(schema.value(), 1, "table_id", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 2, "name", LogicalType::Kind::String, false, false, false, CATALOG_NAME_CAPACITY);
    require_column(schema.value(), 3, "root_page_id", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 4, "unique", LogicalType::Kind::Boolean, false, false, false);
    require_column(schema.value(), 5, "primary", LogicalType::Kind::Boolean, false, false, false);
    require_column(schema.value(), 6, "internal", LogicalType::Kind::Boolean, false, false, false);
}

TEST_CASE("SystemTables defines dandb_index_columns schema", "[catalog][system-tables]") {
    auto schema = SystemTables::index_columns_schema();

    REQUIRE(schema.ok());
    REQUIRE(schema.value().column_count() == 3);
    REQUIRE(schema.value().primary_key_ordinal() == 0);
    REQUIRE(schema.value().primary_key_column().name() == "index_id");

    require_column(schema.value(), 0, "index_id", LogicalType::Kind::Int64, false, true, true);
    require_column(schema.value(), 1, "column_id", LogicalType::Kind::Int64, false, false, false);
    require_column(schema.value(), 2, "ordinal", LogicalType::Kind::Int64, false, false, false);
}
