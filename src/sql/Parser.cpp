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

        core::Status make_incomplete_input_error(SourceLocation location, std::string message) {
            return core::Status::IncompleteInput(
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

    core::Result<std::vector<Statement>> Parser::parse() {

        std::vector<Statement> statements;

        while(!is_at_end()) {

            auto statement_result = parse_statement();
            if(!statement_result.ok()) return statement_result.status();

            const auto semicolon_status = expect_kind(TokenKind::Semicolon, "expected ';' after statement");
            if(!semicolon_status.ok()) return semicolon_status;

            statements.push_back(std::move(statement_result.value()));
            
        }

        return statements;

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

        const auto create_token = current_token();
        auto status = expect_kind(TokenKind::Create, "expected 'CREATE'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Table, "expected 'TABLE' after 'CREATE'");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'CREATE TABLE'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::LeftParen, "expected '(' after table name");
        if(!status.ok()) return status;

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

        status = expect_kind(TokenKind::RightParen, "expected ')' after column definitions");
        if(!status.ok()) return status;

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

        auto status = expect_kind(TokenKind::Create, "expected 'CREATE'");
        if(!status.ok()) return status;

        const bool unique = match_kind(TokenKind::Unique);

        status = expect_kind(TokenKind::Index, "expected 'INDEX' after 'CREATE'");
        if(!status.ok()) return status;

        const auto index_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected index name after 'CREATE INDEX'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::On, "expected 'ON' after index name");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'ON'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::LeftParen, "expected '(' after table name");
        if(!status.ok()) return status;

        const auto column_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected column name inside index definition");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::RightParen, "expected ')' after indexed column");
        if(!status.ok()) return status;

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

        const auto column_name_token = current_token();
        const auto status = expect_kind(TokenKind::Identifier, "expected identifier");
        if(!status.ok()) return status;

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

                auto status = expect_kind(TokenKind::LeftParen, "expected '(' after 'STRING'");
                if(!status.ok()) return status;

                const auto capacity_token = current_token();
                status = expect_kind(TokenKind::IntegerLiteral, "expected capacity inside 'STRING(...)'");
                if(!status.ok()) return status;

                status = expect_kind(TokenKind::RightParen, "expected ')' after string capacity");
                if(!status.ok()) return status;

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
                if(is_at_end()) return make_incomplete_input_error(current_token().location, "expected column type");
                return make_parser_error(current_token().location, "expected column type");

        }

    }

    core::Result<ColumnConstraints> Parser::parse_column_constraints() {

        ColumnConstraints constraints{};
        bool has_next_constraint = true;

        while(has_next_constraint) {
            switch(current_token().kind) {
                case TokenKind::Primary: {
                    consume_token();
                    const auto status = expect_kind(TokenKind::Key, "expected 'KEY' after 'PRIMARY'");
                    if(!status.ok()) return status;
                    constraints.primary_key = true;
                    break;
                }
                case TokenKind::Unique: {
                    consume_token();
                    constraints.unique = true;
                    break;
                }
                case TokenKind::Not: {
                    consume_token();
                    const auto status = expect_kind(TokenKind::NullLiteral, "expected 'NULL' after 'NOT'");
                    if(!status.ok()) return status;
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

        const auto insert_token = current_token();
        auto status = expect_kind(TokenKind::Insert, "expected 'INSERT'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Into, "expected 'INTO' after 'INSERT'");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'INSERT INTO'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Values, "expected 'VALUES' after table name");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::LeftParen, "expected '(' after 'VALUES'");
        if(!status.ok()) return status;

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

        status = expect_kind(TokenKind::RightParen, "expected ')' after values");
        if(!status.ok()) return status;

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

        auto status = expect_kind(TokenKind::Select, "expected 'SELECT'");
        if(!status.ok()) return status;

        auto projection_result = parse_select_projection();
        if(!projection_result.ok()) return projection_result.status();

        status = expect_kind(TokenKind::From, "expected 'FROM' after projection");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'FROM'");
        if(!status.ok()) return status;

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

        auto column_token = current_token();
        auto status = expect_kind(TokenKind::Identifier, "expected '*' or column name after 'SELECT'");
        if(!status.ok()) return status;
        
        std::vector<Identifier> columns;
        columns.push_back(Identifier{ column_token.lexeme, column_token.location });

        while(current_token().kind == TokenKind::Comma) {
            consume_token();

            column_token = current_token();
            status = expect_kind(TokenKind::Identifier, "expected column name after ','");
            if(!status.ok()) return status;
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

        auto status = expect_kind(TokenKind::Update, "expected 'UPDATE'");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'UPDATE'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Set, "expected 'SET' after table name");
        if(!status.ok()) return status;

        const auto column_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected column name after 'SET'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Equal, "expected '=' after column name");
        if(!status.ok()) return status;

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

        auto status = expect_kind(TokenKind::Delete, "expected 'DELETE'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::From, "expected 'FROM' after 'DELETE'");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'DELETE FROM'");
        if(!status.ok()) return status;

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

        auto status = expect_kind(TokenKind::Drop, "expected 'DROP'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Table, "expected 'TABLE' after 'DROP'");
        if(!status.ok()) return status;

        const auto table_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected table name after 'DROP TABLE'");
        if(!status.ok()) return status;

        return Statement{
            DropTableStatement{
                Identifier{table_name_token.lexeme, table_name_token.location},
                location
            }
        };

    }

    core::Result<Statement> Parser::parse_drop_index_statement() {

        const auto location = current_token().location;

        auto status = expect_kind(TokenKind::Drop, "expected 'DROP'");
        if(!status.ok()) return status;

        status = expect_kind(TokenKind::Index, "expected 'INDEX' after 'DROP'");
        if(!status.ok()) return status;

        const auto index_name_token = current_token();
        status = expect_kind(TokenKind::Identifier, "expected index name after 'DROP INDEX'");
        if(!status.ok()) return status;

        return Statement{
            DropIndexStatement{
                Identifier{index_name_token.lexeme, index_name_token.location},
                location
            }
        };

    }

    core::Result<Predicate> Parser::parse_predicate() {

        const auto location = current_token().location;

        const auto column_name_token = current_token();
        auto status = expect_kind(TokenKind::Identifier, "expected column name after 'WHERE'");
        if(!status.ok()) return status;

        if(match_kind(TokenKind::Is)) {
            if(match_kind(TokenKind::NullLiteral)) {
                return Predicate{
                    Identifier{column_name_token.lexeme, column_name_token.location},
                    ComparisonOperator::IsNull,
                    std::nullopt,
                    location
                };
            } else if(match_kind(TokenKind::Not)) {
                status = expect_kind(TokenKind::NullLiteral, "expected 'NULL' after 'IS NOT'");
                if(!status.ok()) return status;
                return Predicate{
                    Identifier{column_name_token.lexeme, column_name_token.location},
                    ComparisonOperator::IsNotNull,
                    std::nullopt,
                    location
                };
            } else {
                if(is_at_end()) return make_incomplete_input_error(current_token().location, "expected 'NULL' or 'NOT' after 'IS'");
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
                if(is_at_end()) return make_incomplete_input_error(current_token().location, "expected comparison operator");
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
                if(is_at_end()) return make_incomplete_input_error(current_token().location, "expected literal");
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

    core::Status Parser::expect_kind(TokenKind expected_kind, std::string_view message) {

        if(is_at_end() && expected_kind != TokenKind::EndOfInput) {
            return make_incomplete_input_error(current_token().location, std::string(message));
        }

        if(current_token().kind != expected_kind) {
            return make_parser_error(current_token().location, std::string(message));
        }

        consume_token();
        return core::Status::Ok();

    }

}
