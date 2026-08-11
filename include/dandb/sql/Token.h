#pragma once

#include <cstddef>
#include <string>

namespace dandb::sql {

    enum class TokenKind {
        Create,
        Table,
        Primary,
        Key,
        Unique,
        Drop,
        Index,
        On,
        Insert,
        Into,
        Values,
        Select,
        From,
        Where,
        Update,
        Set,
        Delete,

        Begin,
        Commit,
        Rollback,
        Checkpoint,

        Int8,
        Int16,
        Int32,
        Int64,
        Double,
        String,
        Bool,

        Is,
        Not,

        Identifier,
        IntegerLiteral,
        DoubleLiteral,
        StringLiteral,
        BooleanLiteral,
        NullLiteral,

        Semicolon,
        Comma,
        LeftParen,
        RightParen,
        Asterisk,
        Equal,
        Greater,
        GreaterEqual,
        Less,
        LessEqual,
        NotEqual,

        EndOfInput
    };

    struct SourceLocation {
        std::size_t line;
        std::size_t column;
    };

    struct Token {
        TokenKind kind;
        std::string lexeme;
        SourceLocation location;
    };

}
