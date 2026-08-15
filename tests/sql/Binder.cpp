#include <catch_amalgamated.hpp>

#include <dandb/catalog/Catalog.h>
#include <dandb/core/Status.h>
#include <dandb/record/Column.h>
#include <dandb/record/LogicalType.h>
#include <dandb/record/Schema.h>
#include <dandb/sql/Binder.h>
#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using dandb::catalog::Catalog;
using dandb::core::StatusCode;
using dandb::record::Column;
using dandb::record::LogicalType;
using dandb::record::Schema;
using dandb::sql::BeginStatement;
using dandb::sql::Binder;
using dandb::sql::BoundCreateIndexStatement;
using dandb::sql::BoundDeleteStatement;
using dandb::sql::BoundDropTableStatement;
using dandb::sql::BoundInsertStatement;
using dandb::sql::BoundSelectStatement;
using dandb::sql::BoundUpdateStatement;
using dandb::sql::ComparisonOperator;
using dandb::sql::Lexer;
using dandb::sql::Parser;
using dandb::sql::Statement;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::size_t TEST_BPM_CAPACITY = 10;

    Statement parse_statement(std::string_view source) {
        Lexer lexer(source);
        auto tokens_result = lexer.tokenize();
        REQUIRE(tokens_result.ok());

        Parser parser(std::move(tokens_result.value()));
        auto statements_result = parser.parse();
        REQUIRE(statements_result.ok());
        REQUIRE(statements_result.value().size() == 1);

        return std::move(statements_result.value().front());
    }

    Schema make_users_schema() {
        auto id_result = Column::create("id", LogicalType::int64(), false, true, true);
        REQUIRE(id_result.ok());

        auto name_type_result = LogicalType::string(64);
        REQUIRE(name_type_result.ok());

        auto name_result = Column::create("name", name_type_result.value(), false, false, false);
        REQUIRE(name_result.ok());

        auto active_result = Column::create("active", LogicalType::boolean(), false, false, false);
        REQUIRE(active_result.ok());

        auto schema_result = Schema::create({
            std::move(id_result.value()),
            std::move(name_result.value()),
            std::move(active_result.value())
        });
        REQUIRE(schema_result.ok());

        return std::move(schema_result.value());
    }

}

TEST_CASE("Binder resolves and validates SQL statements", "[sql][binder]") {
    const TempDir temp_dir;

    auto pager_result = Pager::create(temp_dir.database_path(), TEST_BPM_CAPACITY);
    REQUIRE(pager_result.ok());
    Pager& pager = pager_result.value();

    auto catalog_result = Catalog::load(pager);
    REQUIRE(catalog_result.ok());
    Catalog& catalog = catalog_result.value();

    REQUIRE(catalog.create_table("users", make_users_schema()).ok());

    const auto* users_table = catalog.find_table("users");
    REQUIRE(users_table != nullptr);
    const auto users_table_id = users_table->table_id();

    Binder binder(catalog);

    SECTION("rejects SELECT for a missing table") {
        const auto bound_result = binder.bind(parse_statement("SELECT * FROM missing;"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::NotFound);
        REQUIRE(bound_result.status().message().find("line 1, column 15") != std::string::npos);
    }

    SECTION("rejects a missing SELECT projection column") {
        const auto bound_result = binder.bind(parse_statement("SELECT missing FROM users;"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::NotFound);
        REQUIRE(bound_result.status().message().find("line 1, column 8") != std::string::npos);
    }

    SECTION("expands SELECT all columns in schema order") {
        const auto bound_result = binder.bind(parse_statement("SELECT * FROM users;"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundSelectStatement>(bound_result.value()));

        const auto& statement = std::get<BoundSelectStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.projection.size() == 3);
        REQUIRE(statement.projection[0].ordinal == 0);
        REQUIRE(statement.projection[1].ordinal == 1);
        REQUIRE(statement.projection[2].ordinal == 2);
        REQUIRE_FALSE(statement.predicate.has_value());
    }

    SECTION("preserves SELECT projection order and binds its predicate") {
        const auto bound_result = binder.bind(parse_statement("SELECT name, id FROM users WHERE id = 7;"));

        REQUIRE(bound_result.ok());
        const auto& statement = std::get<BoundSelectStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.projection.size() == 2);
        REQUIRE(statement.projection[0].ordinal == 1);
        REQUIRE(statement.projection[1].ordinal == 0);
        REQUIRE(statement.predicate.has_value());
        REQUIRE(statement.predicate->column.ordinal == 0);
        REQUIRE(statement.predicate->comparison_operator == ComparisonOperator::Equal);
        REQUIRE(statement.predicate->literal.has_value());
        REQUIRE(statement.predicate->literal->as_integer() == 7);
    }

    SECTION("rejects INSERT with the wrong value count") {
        const auto bound_result = binder.bind(parse_statement("INSERT INTO users VALUES (1, 'Ada');"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::InvalidArgument);
        REQUIRE(bound_result.status().message().find("line 1, column 1") != std::string::npos);
    }

    SECTION("binds a complete INSERT tuple") {
        const auto bound_result = binder.bind(parse_statement("INSERT INTO users VALUES (1, 'Ada', TRUE);"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundInsertStatement>(bound_result.value()));

        const auto& statement = std::get<BoundInsertStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.values.size() == 3);
        REQUIRE(statement.values[0].as_integer() == 1);
        REQUIRE(statement.values[1].as_string() == "Ada");
        REQUIRE(statement.values[2].as_boolean());
    }

    SECTION("rejects an UPDATE of the primary key") {
        const auto bound_result = binder.bind(parse_statement("UPDATE users SET id = 2;"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::InvalidArgument);
        REQUIRE(bound_result.status().message().find("line 1, column 18") != std::string::npos);
    }

    SECTION("binds an UPDATE assignment and predicate") {
        const auto bound_result = binder.bind(parse_statement("UPDATE users SET name = 'Ada' WHERE id = 1;"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundUpdateStatement>(bound_result.value()));

        const auto& statement = std::get<BoundUpdateStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.assignment.column.ordinal == 1);
        REQUIRE(statement.assignment.value.as_string() == "Ada");
        REQUIRE(statement.predicate.has_value());
        REQUIRE(statement.predicate->column.ordinal == 0);
        REQUIRE(statement.predicate->literal->as_integer() == 1);
    }

    SECTION("rejects a write to a system table") {
        const auto bound_result = binder.bind(parse_statement("DELETE FROM dandb_tables;"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::InvalidArgument);
    }

    SECTION("binds a DELETE predicate without a literal") {
        const auto bound_result = binder.bind(parse_statement("DELETE FROM users WHERE active IS NOT NULL;"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundDeleteStatement>(bound_result.value()));

        const auto& statement = std::get<BoundDeleteStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.predicate.has_value());
        REQUIRE(statement.predicate->column.ordinal == 2);
        REQUIRE(statement.predicate->comparison_operator == ComparisonOperator::IsNotNull);
        REQUIRE_FALSE(statement.predicate->literal.has_value());
    }

    SECTION("binds a CREATE INDEX target column") {
        const auto bound_result = binder.bind(parse_statement("CREATE UNIQUE INDEX users_by_name ON users(name);"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundCreateIndexStatement>(bound_result.value()));

        const auto& statement = std::get<BoundCreateIndexStatement>(bound_result.value());
        REQUIRE(statement.index_name == "users_by_name");
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.indexed_column.ordinal == 1);
        REQUIRE(statement.unique);
    }

    SECTION("rejects a missing CREATE INDEX target column") {
        const auto bound_result = binder.bind(parse_statement("CREATE INDEX users_by_missing ON users(missing);"));

        REQUIRE_FALSE(bound_result.ok());
        REQUIRE(bound_result.status().code() == StatusCode::NotFound);
    }

    SECTION("binds a DROP TABLE target") {
        const auto bound_result = binder.bind(parse_statement("DROP TABLE users;"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BoundDropTableStatement>(bound_result.value()));

        const auto& statement = std::get<BoundDropTableStatement>(bound_result.value());
        REQUIRE(statement.table_id == users_table_id);
        REQUIRE(statement.table_name == "users");
    }

    SECTION("passes transaction statements through unchanged") {
        const auto bound_result = binder.bind(parse_statement("BEGIN;"));

        REQUIRE(bound_result.ok());
        REQUIRE(std::holds_alternative<BeginStatement>(bound_result.value()));
    }

    REQUIRE(pager.close().ok());
}
