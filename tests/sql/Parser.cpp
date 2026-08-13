#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>

#include <string>
#include <string_view>
#include <utility>
#include <variant>

using dandb::core::StatusCode;
using dandb::sql::BeginStatement;
using dandb::sql::CheckpointStatement;
using dandb::sql::CommitStatement;
using dandb::sql::CreateIndexStatement;
using dandb::sql::CreateTableStatement;
using dandb::sql::DropIndexStatement;
using dandb::sql::DropTableStatement;
using dandb::sql::InsertStatement;
using dandb::sql::Lexer;
using dandb::sql::Parser;
using dandb::sql::RollbackStatement;

namespace {

    dandb::core::Result<dandb::sql::Statement> parse_sql(std::string_view source) {
        Lexer lexer(source);
        auto tokens_result = lexer.tokenize();

        REQUIRE(tokens_result.ok());

        Parser parser(std::move(tokens_result.value()));
        return parser.parse();
    }

}

TEST_CASE("Parser parses a BEGIN statement", "[sql][parser]") {
    const auto statement_result = parse_sql("BEGIN;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<BeginStatement>(statement_result.value()));
}

TEST_CASE("Parser parses a COMMIT statement", "[sql][parser]") {
    const auto statement_result = parse_sql("COMMIT;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<CommitStatement>(statement_result.value()));
}

TEST_CASE("Parser parses a ROLLBACK statement", "[sql][parser]") {
    const auto statement_result = parse_sql("ROLLBACK;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<RollbackStatement>(statement_result.value()));
}

TEST_CASE("Parser parses a CHECKPOINT statement", "[sql][parser]") {
    const auto statement_result = parse_sql("CHECKPOINT;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<CheckpointStatement>(statement_result.value()));
}

TEST_CASE("Parser parses a CREATE TABLE statement", "[sql][parser]") {
    const auto statement_result = parse_sql(
        "CREATE TABLE users ("
        "small INT8, "
        "medium INT16, "
        "large INT32, "
        "id INT64 PRIMARY KEY, "
        "rating DOUBLE, "
        "name STRING(64) NOT NULL, "
        "active BOOL UNIQUE"
        ");"
    );

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<CreateTableStatement>(statement_result.value()));

    const auto& statement = std::get<CreateTableStatement>(statement_result.value());
    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.columns.size() == 7);
    REQUIRE(statement.columns[3].constraints.primary_key);
    REQUIRE(statement.columns[5].type.logical_type.capacity() == 64);
    REQUIRE(statement.columns[5].constraints.not_null);
    REQUIRE(statement.columns[6].constraints.unique);
}

TEST_CASE("Parser rejects an unknown CREATE TABLE column type", "[sql][parser]") {
    const auto statement_result = parse_sql("CREATE TABLE users (id UNKNOWN);");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
}

TEST_CASE("Parser rejects DEFAULT in a CREATE TABLE statement", "[sql][parser]") {
    const auto statement_result = parse_sql("CREATE TABLE users (id INT64 DEFAULT 1);");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
}

TEST_CASE("Parser parses a DROP TABLE statement", "[sql][parser]") {
    const auto statement_result = parse_sql("DROP TABLE users;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<DropTableStatement>(statement_result.value()));

    const auto& statement = std::get<DropTableStatement>(statement_result.value());
    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.location.line == 1);
    REQUIRE(statement.location.column == 1);
    REQUIRE(statement.table_name.location.line == 1);
    REQUIRE(statement.table_name.location.column == 12);
}

TEST_CASE("Parser parses a DROP INDEX statement", "[sql][parser]") {
    const auto statement_result = parse_sql("DROP INDEX email_lookup;");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<DropIndexStatement>(statement_result.value()));

    const auto& statement = std::get<DropIndexStatement>(statement_result.value());
    REQUIRE(statement.index_name.text == "email_lookup");
}

TEST_CASE("Parser parses a CREATE INDEX statement", "[sql][parser]") {
    const auto statement_result = parse_sql("CREATE INDEX email_lookup ON users(email);");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<CreateIndexStatement>(statement_result.value()));

    const auto& statement = std::get<CreateIndexStatement>(statement_result.value());
    REQUIRE(statement.index_name.text == "email_lookup");
    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.column_name.text == "email");
    REQUIRE_FALSE(statement.unique);
}

TEST_CASE("Parser parses a CREATE UNIQUE INDEX statement", "[sql][parser]") {
    const auto statement_result = parse_sql("CREATE UNIQUE INDEX email_lookup ON users(email);");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<CreateIndexStatement>(statement_result.value()));

    const auto& statement = std::get<CreateIndexStatement>(statement_result.value());
    REQUIRE(statement.index_name.text == "email_lookup");
    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.column_name.text == "email");
    REQUIRE(statement.unique);
}

TEST_CASE("Parser parses an INSERT statement with literal values", "[sql][parser]") {
    const auto statement_result = parse_sql(
        "INSERT INTO users VALUES (1, 3.5, 'Ada', true);"
    );

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<InsertStatement>(statement_result.value()));

    const auto& statement = std::get<InsertStatement>(statement_result.value());
    REQUIRE(statement.table_name.text == "users");
    REQUIRE(statement.values.size() == 4);
    REQUIRE(statement.values[0].value.as_integer() == 1);
    REQUIRE(statement.values[1].value.as_real() == 3.5);
    REQUIRE(statement.values[2].value.as_string() == "Ada");
    REQUIRE(statement.values[3].value.as_boolean());
}

TEST_CASE("Parser parses a NULL value in an INSERT statement", "[sql][parser]") {
    const auto statement_result = parse_sql("INSERT INTO users VALUES (NULL);");

    REQUIRE(statement_result.ok());
    REQUIRE(std::holds_alternative<InsertStatement>(statement_result.value()));

    const auto& statement = std::get<InsertStatement>(statement_result.value());
    REQUIRE(statement.values.size() == 1);
    REQUIRE(statement.values[0].value.is_null());
}

TEST_CASE("Parser rejects an out-of-range integer literal in an INSERT statement", "[sql][parser]") {
    const auto statement_result = parse_sql(
        "INSERT INTO users VALUES (9223372036854775808);"
    );

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
    REQUIRE(statement_result.status().message() ==
        "SQL error at line 1, column 27: integer literal is out of range");
}

TEST_CASE("Parser rejects an out-of-range double literal in an INSERT statement", "[sql][parser]") {
    const auto statement_result = parse_sql(
        "INSERT INTO users VALUES ("+std::string(400, '9')+".0);"
    );

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
    REQUIRE(statement_result.status().message() ==
        "SQL error at line 1, column 27: double literal is out of range");
}

TEST_CASE("Parser rejects an INSERT statement with a named column list", "[sql][parser]") {
    const auto statement_result = parse_sql("INSERT INTO users (id) VALUES (1);");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
}

TEST_CASE("Parser rejects an INSERT statement with multiple tuples", "[sql][parser]") {
    const auto statement_result = parse_sql("INSERT INTO users VALUES (1), (2);");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
}

TEST_CASE("Parser rejects a CREATE INDEX statement with multiple columns", "[sql][parser]") {
    const auto statement_result = parse_sql(
        "CREATE INDEX full_name_lookup ON users(first_name, last_name);"
    );

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
}

TEST_CASE("Parser rejects a transaction statement without a semicolon", "[sql][parser]") {
    const auto statement_result = parse_sql("BEGIN");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
    REQUIRE(statement_result.status().message() == "SQL error at line 1, column 6: expected ';' after statement");
}

TEST_CASE("Parser rejects an unknown statement", "[sql][parser]") {
    const auto statement_result = parse_sql("SELECT;");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
    REQUIRE(statement_result.status().message() == "SQL error at line 1, column 1: expected statement");
}

TEST_CASE("Parser rejects tokens after a complete statement", "[sql][parser]") {
    const auto statement_result = parse_sql("BEGIN; COMMIT;");

    REQUIRE_FALSE(statement_result.ok());
    REQUIRE(statement_result.status().code() == StatusCode::ParseError);
    REQUIRE(statement_result.status().message() == "SQL error at line 1, column 8: unexpected token after ';'");
}
