#include <catch_amalgamated.hpp>

#include <dandb/sql/Token.h>

#include <array>

using dandb::sql::TokenKind;

TEST_CASE("TokenKind names every token supported by DanDB SQL", "[sql][token]") {
    const std::array token_kinds{
        TokenKind::Create,
        TokenKind::Table,
        TokenKind::Primary,
        TokenKind::Key,
        TokenKind::Unique,
        TokenKind::Drop,
        TokenKind::Index,
        TokenKind::On,
        TokenKind::Insert,
        TokenKind::Into,
        TokenKind::Values,
        TokenKind::Select,
        TokenKind::From,
        TokenKind::Where,
        TokenKind::Update,
        TokenKind::Set,
        TokenKind::Delete,
        TokenKind::Begin,
        TokenKind::Commit,
        TokenKind::Rollback,
        TokenKind::Checkpoint,
        TokenKind::Int8,
        TokenKind::Int16,
        TokenKind::Int32,
        TokenKind::Int64,
        TokenKind::Double,
        TokenKind::String,
        TokenKind::Bool,
        TokenKind::Is,
        TokenKind::Not,
        TokenKind::Identifier,
        TokenKind::IntegerLiteral,
        TokenKind::DoubleLiteral,
        TokenKind::StringLiteral,
        TokenKind::BooleanLiteral,
        TokenKind::NullLiteral,
        TokenKind::Semicolon,
        TokenKind::Comma,
        TokenKind::LeftParen,
        TokenKind::RightParen,
        TokenKind::Asterisk,
        TokenKind::Equal,
        TokenKind::Greater,
        TokenKind::GreaterEqual,
        TokenKind::Less,
        TokenKind::LessEqual,
        TokenKind::NotEqual,
        TokenKind::EndOfInput,
    };

    REQUIRE(token_kinds.size() == 48);
}
