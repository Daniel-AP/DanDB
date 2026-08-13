#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/sql/Ast.h>
#include <dandb/sql/Token.h>

#include <cstddef>
#include <vector>

namespace dandb::sql {

    class Parser {
        public:
            explicit Parser(std::vector<Token> tokens);

            core::Result<std::vector<Statement>> parse();

        private:
            std::vector<Token> tokens_;
            std::size_t current_pos_ = 0;

            core::Result<Statement> parse_statement();

            core::Result<Statement> parse_transaction_statement();

            core::Result<Statement> parse_create_table_statement();
            core::Result<Statement> parse_create_index_statement();
            core::Result<ColumnDefinition> parse_column_definition();
            core::Result<ColumnType> parse_column_type();
            core::Result<ColumnConstraints> parse_column_constraints();

            core::Result<Statement> parse_insert_statement();

            core::Result<Statement> parse_select_statement();
            core::Result<SelectProjection> parse_select_projection();

            core::Result<Statement> parse_update_statement();

            core::Result<Statement> parse_delete_statement();

            core::Result<Statement> parse_drop_table_statement();

            core::Result<Statement> parse_drop_index_statement();

            core::Result<Predicate> parse_predicate();
            core::Result<ComparisonOperator> parse_comparison_operator();
            core::Result<LiteralExpression> parse_literal();

            bool is_at_end() const;
            const Token& current_token() const;
            const Token& consume_token();
            bool match_kind(TokenKind expected_kind);
            core::Status expect_kind(TokenKind expected_kind, std::string_view message);
    };

}
