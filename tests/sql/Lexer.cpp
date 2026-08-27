#include <catch_amalgamated.hpp>

#include <dandb/core/Status.h>
#include <dandb/sql/Lexer.h>

using dandb::core::StatusCode;
using dandb::sql::Lexer;
using dandb::sql::TokenKind;

TEST_CASE("Lexer recognizes a mixed-case CREATE keyword", "[sql][lexer]") {
    Lexer lexer("cReAtE");

    const auto result = lexer.tokenize();

    REQUIRE(result.ok());

    const auto& tokens = result.value();

    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0].kind == TokenKind::Create);
    REQUIRE(tokens[0].lexeme == "cReAtE");
    REQUIRE(tokens[0].location.line == 1);
    REQUIRE(tokens[0].location.column == 1);
    REQUIRE(tokens[1].kind == TokenKind::EndOfInput);
}

TEST_CASE("Lexer tokenizes a CREATE TABLE statement", "[sql][lexer]") {
    Lexer lexer("CREATE TABLE users (id INT64 PRIMARY KEY);");

    const auto result = lexer.tokenize();

    REQUIRE(result.ok());

    const auto& tokens = result.value();

    REQUIRE(tokens.size() == 11);
    REQUIRE(tokens[0].kind == TokenKind::Create);
    REQUIRE(tokens[1].kind == TokenKind::Table);
    REQUIRE(tokens[2].kind == TokenKind::Identifier);
    REQUIRE(tokens[2].lexeme == "users");
    REQUIRE(tokens[3].kind == TokenKind::LeftParen);
    REQUIRE(tokens[4].kind == TokenKind::Identifier);
    REQUIRE(tokens[4].lexeme == "id");
    REQUIRE(tokens[5].kind == TokenKind::Int64);
    REQUIRE(tokens[6].kind == TokenKind::Primary);
    REQUIRE(tokens[7].kind == TokenKind::Key);
    REQUIRE(tokens[8].kind == TokenKind::RightParen);
    REQUIRE(tokens[9].kind == TokenKind::Semicolon);
    REQUIRE(tokens[10].kind == TokenKind::EndOfInput);
}

TEST_CASE("Lexer tokenizes an INSERT statement", "[sql][lexer]") {
    Lexer lexer("INSERT INTO users VALUES (1, 3.5, 'Ada', true, NULL);");

    const auto result = lexer.tokenize();

    REQUIRE(result.ok());

    const auto& tokens = result.value();

    REQUIRE(tokens.size() == 17);
    REQUIRE(tokens[0].kind == TokenKind::Insert);
    REQUIRE(tokens[1].kind == TokenKind::Into);
    REQUIRE(tokens[2].kind == TokenKind::Identifier);
    REQUIRE(tokens[2].lexeme == "users");
    REQUIRE(tokens[3].kind == TokenKind::Values);
    REQUIRE(tokens[4].kind == TokenKind::LeftParen);
    REQUIRE(tokens[5].kind == TokenKind::IntegerLiteral);
    REQUIRE(tokens[5].lexeme == "1");
    REQUIRE(tokens[6].kind == TokenKind::Comma);
    REQUIRE(tokens[7].kind == TokenKind::DoubleLiteral);
    REQUIRE(tokens[7].lexeme == "3.5");
    REQUIRE(tokens[8].kind == TokenKind::Comma);
    REQUIRE(tokens[9].kind == TokenKind::StringLiteral);
    REQUIRE(tokens[9].lexeme == "Ada");
    REQUIRE(tokens[10].kind == TokenKind::Comma);
    REQUIRE(tokens[11].kind == TokenKind::BooleanLiteral);
    REQUIRE(tokens[11].lexeme == "true");
    REQUIRE(tokens[12].kind == TokenKind::Comma);
    REQUIRE(tokens[13].kind == TokenKind::NullLiteral);
    REQUIRE(tokens[13].lexeme == "NULL");
    REQUIRE(tokens[14].kind == TokenKind::RightParen);
    REQUIRE(tokens[15].kind == TokenKind::Semicolon);
    REQUIRE(tokens[16].kind == TokenKind::EndOfInput);
}

TEST_CASE("Lexer tokenizes a SELECT statement with a WHERE clause", "[sql][lexer]") {
    Lexer lexer("SELECT id, name FROM users WHERE id >= 10;");

    const auto result = lexer.tokenize();

    REQUIRE(result.ok());

    const auto& tokens = result.value();

    REQUIRE(tokens.size() == 12);
    REQUIRE(tokens[0].kind == TokenKind::Select);
    REQUIRE(tokens[1].kind == TokenKind::Identifier);
    REQUIRE(tokens[1].lexeme == "id");
    REQUIRE(tokens[2].kind == TokenKind::Comma);
    REQUIRE(tokens[3].kind == TokenKind::Identifier);
    REQUIRE(tokens[3].lexeme == "name");
    REQUIRE(tokens[4].kind == TokenKind::From);
    REQUIRE(tokens[5].kind == TokenKind::Identifier);
    REQUIRE(tokens[5].lexeme == "users");
    REQUIRE(tokens[6].kind == TokenKind::Where);
    REQUIRE(tokens[7].kind == TokenKind::Identifier);
    REQUIRE(tokens[7].lexeme == "id");
    REQUIRE(tokens[8].kind == TokenKind::GreaterEqual);
    REQUIRE(tokens[9].kind == TokenKind::IntegerLiteral);
    REQUIRE(tokens[9].lexeme == "10");
    REQUIRE(tokens[10].kind == TokenKind::Semicolon);
    REQUIRE(tokens[11].kind == TokenKind::EndOfInput);
}

TEST_CASE("Lexer reports incomplete input for an unterminated string literal", "[sql][lexer]") {
    Lexer lexer("'Ada");

    const auto result = lexer.tokenize();

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::IncompleteInput);
    REQUIRE(result.status().message() == "SQL error at line 1, column 1: unterminated string literal");
}

TEST_CASE("Lexer reports the location of an invalid character", "[sql][lexer]") {
    Lexer lexer("SELECT\n@");

    const auto result = lexer.tokenize();

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::ParseError);
    REQUIRE(result.status().message() == "SQL error at line 2, column 1: unexpected character '@'");
}
