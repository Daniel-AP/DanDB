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
