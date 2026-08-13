#include <dandb/sql/Parser.h>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dandb::sql {

    namespace {

        core::Status make_parser_error(SourceLocation location, std::string message) {
            return core::Status::ParseError(
                "SQL error at line "+std::to_string(location.line)+
                ", column "+std::to_string(location.column)+": "+
                std::move(message)
            );
        }

        core::Result<std::int64_t> parse_integer_literal(std::string_view lexeme, SourceLocation location) {

            std::int64_t value = 0;
            const auto result = std::from_chars(lexeme.data(), lexeme.data()+lexeme.size(), value);

            if(result.ec == std::errc::result_out_of_range) {
                return make_parser_error(location, "integer literal is out of range");
            }

            if(result.ec != std::errc{} || result.ptr != lexeme.data()+lexeme.size()) {
                return make_parser_error(location, "invalid integer literal");
            }

            return value;

        }

        core::Result<double> parse_double_literal(std::string_view lexeme, SourceLocation location) {

            double value = 0.0;
            const auto result = std::from_chars(lexeme.data(), lexeme.data()+lexeme.size(), value);

            if(result.ec == std::errc::result_out_of_range) {
                return make_parser_error(location, "double literal is out of range");
            }

            if(result.ec != std::errc{} || result.ptr != lexeme.data()+lexeme.size()) {
                return make_parser_error(location, "invalid double literal");
            }

            return value;
            
        }

    }

    Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    core::Result<Statement> Parser::parse() {

        auto statement_result = parse_statement();
        if(!statement_result.ok()) return statement_result.status();

        if(!match_kind(TokenKind::Semicolon)) {
            return make_parser_error(current_token().location, "expected ';' after statement");
        }

        if(!is_at_end()) {
            return make_parser_error(current_token().location, "unexpected token after ';'");
        }

        return std::move(statement_result.value());

    }

    core::Result<Statement> Parser::parse_statement() {

        switch(current_token().kind) {
            case TokenKind::Begin:
            case TokenKind::Commit:
            case TokenKind::Rollback:
            case TokenKind::Checkpoint:
                return parse_transaction_statement();
            case TokenKind::Create:
                if(tokens_[current_pos_+1].kind == TokenKind::Table) {
                    return parse_create_table_statement();
                }
                return parse_create_index_statement();
            case TokenKind::Drop:
                if(tokens_[current_pos_+1].kind == TokenKind::Table) {
                    return parse_drop_table_statement();
                }
                return parse_drop_index_statement();
            case TokenKind::Insert:
                return parse_insert_statement();
            case TokenKind::Select:
                return parse_select_statement();
            case TokenKind::Update:
                return parse_update_statement();
            case TokenKind::Delete:
                return parse_delete_statement();
            default:
                return make_parser_error(current_token().location, "expected statement");
        }

    }

    core::Result<Statement> Parser::parse_transaction_statement() {

        const SourceLocation location = current_token().location;

        if(match_kind(TokenKind::Begin)) {
            return Statement{ BeginStatement{ location } };
        }

        if(match_kind(TokenKind::Commit)) {
            return Statement{ CommitStatement{ location } };
        }

        if(match_kind(TokenKind::Rollback)) {
            return Statement{ RollbackStatement{ location } };
        }

        if(match_kind(TokenKind::Checkpoint)) {
            return Statement{ CheckpointStatement{ location } };
        }

        return make_parser_error(location, "expected transaction statement");

    }

    core::Result<Statement> Parser::parse_create_table_statement() {

        if(current_token().kind != TokenKind::Create) {
            return make_parser_error(current_token().location, "expected 'CREATE'");
        }
        const auto create_token = consume_token();

        if(current_token().kind != TokenKind::Table) {
            return make_parser_error(current_token().location, "expected 'TABLE' after 'CREATE'");
        }
        consume_token();

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'CREATE TABLE'");
        }
        const auto table_name_token = consume_token();

        if(current_token().kind != TokenKind::LeftParen) {
            return make_parser_error(current_token().location, "expected '(' after table name");
        }
        consume_token();

        auto column_definition_result = parse_column_definition();
        if(!column_definition_result.ok()) return column_definition_result.status();

        std::vector<ColumnDefinition> columns;
        columns.push_back(std::move(column_definition_result.value()));

        while(current_token().kind == TokenKind::Comma) {
            consume_token();

            column_definition_result = parse_column_definition();
            if(!column_definition_result.ok()) return column_definition_result.status();

            columns.push_back(std::move(column_definition_result.value()));
        }

        if(current_token().kind != TokenKind::RightParen) {
            return make_parser_error(current_token().location, "expected ')' after column definitions");
        }
        consume_token();

        return Statement{
            CreateTableStatement{
                Identifier{table_name_token.lexeme, table_name_token.location},
                std::move(columns),
                create_token.location
            }
        };

    }

    core::Result<Statement> Parser::parse_create_index_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Create)) {
            return make_parser_error(current_token().location, "expected 'CREATE'");
        }

        const bool unique = match_kind(TokenKind::Unique);

        if(!match_kind(TokenKind::Index)) {
            return make_parser_error(current_token().location, "expected 'INDEX' after 'CREATE'");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected index name after 'CREATE INDEX'");
        }
        const auto index_name_token = consume_token();

        if(!match_kind(TokenKind::On)) {
            return make_parser_error(current_token().location, "expected 'ON' after index name");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'ON'");
        }
        const auto table_name_token = consume_token();

        if(!match_kind(TokenKind::LeftParen)) {
            return make_parser_error(current_token().location, "expected '(' after table name");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected column name inside index definition");
        }
        const auto column_name_token = consume_token();

        if(!match_kind(TokenKind::RightParen)) {
            return make_parser_error(current_token().location, "expected ')' after indexed column");
        }

        return Statement{
            CreateIndexStatement{
                Identifier{index_name_token.lexeme, index_name_token.location},
                Identifier{table_name_token.lexeme, table_name_token.location},
                Identifier{column_name_token.lexeme, column_name_token.location},
                unique,
                location
            }
        };

    }

    core::Result<ColumnDefinition> Parser::parse_column_definition() {

        const auto location = current_token().location;

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected identifier");
        }
        auto column_name_token = consume_token();

        auto column_type_result = parse_column_type();
        if(!column_type_result.ok()) return column_type_result.status();

        auto column_constraints_result = parse_column_constraints();
        if(!column_constraints_result.ok()) return column_constraints_result.status();

        return ColumnDefinition{
            Identifier{column_name_token.lexeme, column_name_token.location},
            column_type_result.value(),
            column_constraints_result.value(),
            location
        };

    }

    core::Result<ColumnType> Parser::parse_column_type() {

        switch(current_token().kind) {
            case TokenKind::Int8: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::int8(), type_token.location};
            }
            case TokenKind::Int16: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::int16(), type_token.location};
            }
            case TokenKind::Int32: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::int32(), type_token.location};
            }
            case TokenKind::Int64: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::int64(), type_token.location};
            }
            case TokenKind::Double: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::float64(), type_token.location};
            }
            case TokenKind::Bool: {
                const auto type_token = consume_token();
                return ColumnType{record::LogicalType::boolean(), type_token.location};
            }
            case TokenKind::String: {
                const auto type_token = consume_token();

                if(current_token().kind != TokenKind::LeftParen) {
                    return make_parser_error(current_token().location, "expected '(' after 'STRING'");
                }
                consume_token();

                if(current_token().kind != TokenKind::IntegerLiteral) {
                    return make_parser_error(current_token().location, "expected capacity inside 'STRING(...)'");
                }
                const auto capacity_token = consume_token();

                if(current_token().kind != TokenKind::RightParen) {
                    return make_parser_error(current_token().location, "expected ')' after string capacity");
                }
                consume_token();

                const std::size_t max_capacity = static_cast<std::size_t>(-1);
                std::size_t capacity = 0;

                for(const char ch : capacity_token.lexeme) {
                    const std::size_t digit = static_cast<std::size_t>(ch-'0');

                    if(capacity > (max_capacity-digit)/10) {
                        return make_parser_error(capacity_token.location, "string capacity is too large");
                    }

                    capacity = capacity*10+digit;
                }

                auto logical_type_result = record::LogicalType::string(capacity);
                if(!logical_type_result.ok()) {
                    return make_parser_error(capacity_token.location, logical_type_result.status().message());
                }

                return ColumnType{std::move(logical_type_result.value()), type_token.location};
            }
            default:
                return make_parser_error(current_token().location, "expected column type");

        }

    }

    core::Result<ColumnConstraints> Parser::parse_column_constraints() {

        ColumnConstraints constraints{};
        bool has_next_constraint = true;

        while(has_next_constraint) {
            switch(current_token().kind) {
                case TokenKind::Primary: {
                    const auto primary_token = consume_token();
                    if(current_token().kind != TokenKind::Key) {
                        return make_parser_error(current_token().location, "expected 'KEY' after 'PRIMARY'");
                    }
                    consume_token();
                    constraints.primary_key = true;
                    break;
                }
                case TokenKind::Unique: {
                    const auto unique_token = consume_token();
                    constraints.unique = true;
                    break;
                }
                case TokenKind::Not: {
                    const auto not_token = consume_token();
                    if(current_token().kind != TokenKind::NullLiteral) {
                        return make_parser_error(current_token().location, "expected 'NULL' after 'NOT'");
                    }
                    consume_token();
                    constraints.not_null = true;
                    break;
                }
                default:
                    has_next_constraint = false;
            }
        }

        return constraints;

    }

    core::Result<Statement> Parser::parse_insert_statement() {

        if(current_token().kind != TokenKind::Insert) {
            return make_parser_error(current_token().location, "expected 'INSERT'");
        }
        const auto insert_token = consume_token();

        if(current_token().kind != TokenKind::Into) {
            return make_parser_error(current_token().location, "expected 'INTO' after 'INSERT'");
        }
        consume_token();

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'INSERT INTO'");
        }
        const auto table_name_token = consume_token();

        if(current_token().kind != TokenKind::Values) {
            return make_parser_error(current_token().location, "expected 'VALUES' after table name");
        }
        consume_token();

        if(current_token().kind != TokenKind::LeftParen) {
            return make_parser_error(current_token().location, "expected '(' after 'VALUES'");
        }
        consume_token();

        auto literal_result = parse_literal();
        if(!literal_result.ok()) return literal_result.status();

        std::vector<LiteralExpression> values;
        values.push_back(std::move(literal_result.value()));

        while(current_token().kind == TokenKind::Comma) {
            consume_token();

            literal_result = parse_literal();
            if(!literal_result.ok()) return literal_result.status();

            values.push_back(std::move(literal_result.value()));
        }

        if(current_token().kind != TokenKind::RightParen) {
            return make_parser_error(current_token().location, "expected ')' after values");
        }
        consume_token();

        return Statement{
            InsertStatement{
                Identifier{table_name_token.lexeme, table_name_token.location},
                std::move(values),
                insert_token.location
            }
        };

    }

    core::Result<Statement> Parser::parse_select_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Select)) {
            return make_parser_error(current_token().location, "expected 'SELECT'");
        }

        auto projection_result = parse_select_projection();
        if(!projection_result.ok()) return projection_result.status();

        if(!match_kind(TokenKind::From)) {
            return make_parser_error(current_token().location, "expected 'FROM' after projection");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'FROM'");
        }

        auto table_name_token = consume_token();

        SelectStatement statement{
            std::move(projection_result.value()),
            Identifier{table_name_token.lexeme, table_name_token.location},
            std::nullopt,
            location
        };
        
        if(match_kind(TokenKind::Where)) {
            auto predicate_result = parse_predicate();
            if(!predicate_result.ok()) return predicate_result.status();
            statement.predicate = std::move(predicate_result.value());
        }

        return Statement{ std::move(statement) };
        
    }

    core::Result<SelectProjection> Parser::parse_select_projection() {

        const auto location = current_token().location;

        if(match_kind(TokenKind::Asterisk)) {
            return SelectProjection{
                SelectAll{ location }
            };
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected '*' or column name after 'SELECT'");
        }
        auto column_token = consume_token();
        
        std::vector<Identifier> columns;
        columns.push_back(Identifier{ column_token.lexeme, column_token.location });

        while(current_token().kind == TokenKind::Comma) {
            consume_token();

            if(current_token().kind != TokenKind::Identifier) {
                return make_parser_error(current_token().location, "expected column name after ','");
            }

            column_token = consume_token();
            columns.push_back(Identifier{ column_token.lexeme, column_token.location });

        }

        return SelectProjection{
            SelectColumns{
                std::move(columns),
                location
            }
        };

    }

    core::Result<Statement> Parser::parse_update_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Update)) {
            return make_parser_error(current_token().location, "expected 'UPDATE'");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'UPDATE'");
        }
        auto table_name_token = consume_token();

        if(!match_kind(TokenKind::Set)) {
            return make_parser_error(current_token().location, "expected 'SET' after table name");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected column name after 'SET'");
        }
        auto column_name_token = consume_token();

        if(!match_kind(TokenKind::Equal)) {
            return make_parser_error(current_token().location, "expected '=' after column name");
        }

        auto literal_expression_result = parse_literal();
        if(!literal_expression_result.ok()) return literal_expression_result.status();

        UpdateStatement statement{
            Identifier{ table_name_token.lexeme, table_name_token.location },
            Assignment{
                Identifier{ column_name_token.lexeme, column_name_token.location },
                std::move(literal_expression_result.value()),
                column_name_token.location
            },
            std::nullopt,
            location
        };

        if(match_kind(TokenKind::Where)) {
            auto predicate_result = parse_predicate();
            if(!predicate_result.ok()) return predicate_result.status();
            statement.predicate = std::move(predicate_result.value());
        }

        return Statement{ std::move(statement) };

    }

    core::Result<Statement> Parser::parse_delete_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Delete)) {
            return make_parser_error(current_token().location, "expected 'DELETE'");
        }

        if(!match_kind(TokenKind::From)) {
            return make_parser_error(current_token().location, "expected 'FROM' after 'DELETE'");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'DELETE FROM'");
        }
        auto table_name_token = consume_token();

        DeleteStatement statement{
            Identifier{ table_name_token.lexeme, table_name_token.location },
            std::nullopt,
            location
        };

        if(match_kind(TokenKind::Where)) {
            auto predicate_result = parse_predicate();
            if(!predicate_result.ok()) return predicate_result.status();
            statement.predicate = std::move(predicate_result.value());
        }

        return Statement{ std::move(statement) };

    }

    core::Result<Statement> Parser::parse_drop_table_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Drop)) {
            return make_parser_error(current_token().location, "expected 'DROP'");
        }

        if(!match_kind(TokenKind::Table)) {
            return make_parser_error(current_token().location, "expected 'TABLE' after 'DROP'");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected table name after 'DROP TABLE'");
        }

        const auto table_name_token = consume_token();

        return Statement{
            DropTableStatement{
                Identifier{table_name_token.lexeme, table_name_token.location},
                location
            }
        };

    }

    core::Result<Statement> Parser::parse_drop_index_statement() {

        const auto location = current_token().location;

        if(!match_kind(TokenKind::Drop)) {
            return make_parser_error(current_token().location, "expected 'DROP'");
        }

        if(!match_kind(TokenKind::Index)) {
            return make_parser_error(current_token().location, "expected 'INDEX' after 'DROP'");
        }

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected index name after 'DROP INDEX'");
        }
        const auto index_name_token = consume_token();

        return Statement{
            DropIndexStatement{
                Identifier{index_name_token.lexeme, index_name_token.location},
                location
            }
        };

    }

    core::Result<Predicate> Parser::parse_predicate() {

        const auto location = current_token().location;

        if(current_token().kind != TokenKind::Identifier) {
            return make_parser_error(current_token().location, "expected column name after 'WHERE'");
        }

        auto column_name_token = consume_token();

        if(match_kind(TokenKind::Is)) {
            if(match_kind(TokenKind::NullLiteral)) {
                return Predicate{
                    Identifier{column_name_token.lexeme, column_name_token.location},
                    ComparisonOperator::IsNull,
                    std::nullopt,
                    location
                };
            } else if(match_kind(TokenKind::Not)) {
                if(!match_kind(TokenKind::NullLiteral)) {
                    return make_parser_error(current_token().location, "expected 'NULL' after 'IS NOT'");
                }
                return Predicate{
                    Identifier{column_name_token.lexeme, column_name_token.location},
                    ComparisonOperator::IsNotNull,
                    std::nullopt,
                    location
                };
            } else {
                return make_parser_error(current_token().location, "expected 'NULL' or 'NOT' after 'IS'");
            }
        }

        auto comparison_operator_result = parse_comparison_operator();
        if(!comparison_operator_result.ok()) return comparison_operator_result.status();

        auto literal_expression_result = parse_literal();
        if(!literal_expression_result.ok()) return literal_expression_result.status();

        return Predicate{
            Identifier{column_name_token.lexeme, column_name_token.location},
            std::move(comparison_operator_result.value()),
            std::move(literal_expression_result.value()),
            location
        };

    }

    core::Result<ComparisonOperator> Parser::parse_comparison_operator() {

        switch(current_token().kind) {
            case TokenKind::Equal:
                consume_token();
                return ComparisonOperator::Equal;
            case TokenKind::NotEqual:
                consume_token();
                return ComparisonOperator::NotEqual;
            case TokenKind::Less:
                consume_token();
                return ComparisonOperator::Less;
            case TokenKind::LessEqual:
                consume_token();
                return ComparisonOperator::LessEqual;
            case TokenKind::Greater:
                consume_token();
                return ComparisonOperator::Greater;
            case TokenKind::GreaterEqual:
                consume_token();
                return ComparisonOperator::GreaterEqual;
            default:
                return make_parser_error(current_token().location, "expected comparison operator");
        }

    }

    core::Result<LiteralExpression> Parser::parse_literal() {

        switch(current_token().kind) {
            case TokenKind::IntegerLiteral: {
                const auto literal_token = consume_token();
                auto value_result = parse_integer_literal(literal_token.lexeme, literal_token.location);
                if(!value_result.ok()) return value_result.status();

                return LiteralExpression{
                    record::LiteralValue::integer(value_result.value()),
                    literal_token.location
                };
            }
            case TokenKind::DoubleLiteral: {
                const auto literal_token = consume_token();
                auto value_result = parse_double_literal(literal_token.lexeme, literal_token.location);
                if(!value_result.ok()) return value_result.status();

                return LiteralExpression{
                    record::LiteralValue::real(value_result.value()),
                    literal_token.location
                };
            }
            case TokenKind::StringLiteral: {
                const auto literal_token = consume_token();

                return LiteralExpression{
                    record::LiteralValue::string(literal_token.lexeme),
                    literal_token.location
                };
            }
            case TokenKind::BooleanLiteral: {
                const auto literal_token = consume_token();
                const bool value = (literal_token.lexeme[0] == 't' || literal_token.lexeme[0] == 'T');

                return LiteralExpression{
                    record::LiteralValue::boolean(value),
                    literal_token.location
                };
            }
            case TokenKind::NullLiteral: {
                const auto literal_token = consume_token();

                return LiteralExpression{
                    record::LiteralValue::null(),
                    literal_token.location
                };
            }
            default:
                return make_parser_error(current_token().location, "expected literal");
        }

    }

    bool Parser::is_at_end() const {
        return current_token().kind == TokenKind::EndOfInput;
    }

    const Token& Parser::current_token() const {
        return tokens_[current_pos_];
    }

    const Token& Parser::consume_token() {
        const Token& token = current_token();
        current_pos_++;
        return token;
    }

    bool Parser::match_kind(TokenKind expected_kind) {
        if(current_token().kind != expected_kind) return false;

        consume_token();
        return true;
    }

}
