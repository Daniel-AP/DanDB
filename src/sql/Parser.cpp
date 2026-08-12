#include <dandb/sql/Parser.h>

#include <string>
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
                return parse_create_table_statement();
            case TokenKind::Drop:
                return parse_drop_table_statement();
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

            if(is_at_end()) {
                return make_parser_error(current_token().location, "expected column definition after ','");
            }

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
