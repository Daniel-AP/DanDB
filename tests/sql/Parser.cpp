#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/sql/Lexer.h>
#include <dandb/sql/Parser.h>

#include <string_view>
#include <utility>
#include <variant>

using dandb::core::StatusCode;
using dandb::sql::BeginStatement;
using dandb::sql::CheckpointStatement;
using dandb::sql::CommitStatement;
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
