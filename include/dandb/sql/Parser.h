#pragma once

#include <dandb/core/Result.h>
#include <dandb/sql/Ast.h>
#include <dandb/sql/Token.h>

#include <cstddef>
#include <vector>

namespace dandb::sql {

    class Parser {
        public:
            explicit Parser(std::vector<Token> tokens);

            core::Result<Statement> parse();

        private:
            std::vector<Token> tokens_;
            std::size_t current_pos_ = 0;

            core::Result<Statement> parse_statement();
            core::Result<Statement> parse_transaction_statement();

            bool is_at_end() const;
            const Token& current_token() const;
            const Token& consume_token();
            bool match_kind(TokenKind expected_kind);
    };

}
