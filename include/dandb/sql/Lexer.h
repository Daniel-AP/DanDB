#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/sql/Token.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace dandb::sql {

    class Lexer {
        public:
            explicit Lexer(std::string_view source);

            core::Result<std::vector<Token>> tokenize();

        private:
            std::string_view source_;
            std::size_t current_pos_ = 0;
            std::size_t start_pos_ = 0;
            SourceLocation current_location_{1, 1};
            SourceLocation start_location_{1, 1};
            std::vector<Token> tokens_;

            core::Status tokenize_next();
            core::Status tokenize_identifier_or_reserved_word();
            core::Status tokenize_number();
            core::Status tokenize_string();

            bool is_at_end() const;
            char consume_char();
            void add_token(TokenKind kind, std::string_view lexeme, SourceLocation location);
            void add_current_token(TokenKind kind);
            std::optional<char> peek_char() const;
            std::optional<char> peek_next_char() const;
            bool match_char(char expected);
    };

}
