#include <dandb/sql/Lexer.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>

using namespace std::string_literals;

namespace dandb::sql {

    namespace {

        bool is_ascii_digit(char ch) {
            return ch >= '0' && ch <= '9';
        }

        bool is_ascii_letter(char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        }

        bool is_ascii_alphanumeric(char ch) {
            return is_ascii_letter(ch) || is_ascii_digit(ch);
        }

        char to_ascii_upper(char ch) {
            if(ch >= 'a' && ch <= 'z') return ch-'a'+'A';
            return ch;
        }

        const std::unordered_map<std::string_view, TokenKind> RESERVED_WORD_KINDS{
            { "CREATE", TokenKind::Create },
            { "TABLE", TokenKind::Table },
            { "PRIMARY", TokenKind::Primary },
            { "KEY", TokenKind::Key },
            { "UNIQUE", TokenKind::Unique },
            { "DROP", TokenKind::Drop },
            { "INDEX", TokenKind::Index },
            { "ON", TokenKind::On },
            { "INSERT", TokenKind::Insert },
            { "INTO", TokenKind::Into },
            { "VALUES", TokenKind::Values },
            { "SELECT", TokenKind::Select },
            { "FROM", TokenKind::From },
            { "WHERE", TokenKind::Where },
            { "UPDATE", TokenKind::Update },
            { "SET", TokenKind::Set },
            { "DELETE", TokenKind::Delete },
            { "BEGIN", TokenKind::Begin },
            { "COMMIT", TokenKind::Commit },
            { "ROLLBACK", TokenKind::Rollback },
            { "CHECKPOINT", TokenKind::Checkpoint },
            { "INT8", TokenKind::Int8 },
            { "INT16", TokenKind::Int16 },
            { "INT32", TokenKind::Int32 },
            { "INT64", TokenKind::Int64 },
            { "DOUBLE", TokenKind::Double },
            { "STRING", TokenKind::String },
            { "BOOL", TokenKind::Bool },
            { "IS", TokenKind::Is },
            { "NOT", TokenKind::Not },
            { "TRUE", TokenKind::BooleanLiteral },
            { "FALSE", TokenKind::BooleanLiteral },
            { "NULL", TokenKind::NullLiteral },
        };

        core::Status make_lexer_error(SourceLocation location, std::string_view message) {
            return core::Status::ParseError(
                "SQL error at line "+std::to_string(location.line)+
                ", column "+std::to_string(location.column)+": "+
                std::string(message)
            );
        }

    }

    Lexer::Lexer(std::string_view source) : source_(source) {}

    core::Result<std::vector<Token>> Lexer::tokenize() {

        while(!is_at_end()) {

            start_pos_ = current_pos_;
            start_location_ = current_location_;

            auto status = tokenize_next();
            if(!status.ok()) return status;

        }

        add_token(TokenKind::EndOfInput, "", current_location_);

        return std::exchange(tokens_, {});

    }

    core::Status Lexer::tokenize_next() {

        char ch = consume_char();

        switch(ch) {

            case ';': add_current_token(TokenKind::Semicolon); break;
            case ',': add_current_token(TokenKind::Comma); break;
            case '(': add_current_token(TokenKind::LeftParen); break;
            case ')': add_current_token(TokenKind::RightParen); break;
            case '*': add_current_token(TokenKind::Asterisk); break;
            case '=': add_current_token(TokenKind::Equal); break;

            case '<':
                if(match_char('=')) add_current_token(TokenKind::LessEqual);
                else add_current_token(TokenKind::Less);
                break;
            case '>':
                if(match_char('=')) add_current_token(TokenKind::GreaterEqual);
                else add_current_token(TokenKind::Greater);
                break;
            case '!':
                if(match_char('=')) add_current_token(TokenKind::NotEqual);
                else return make_lexer_error(start_location_, "unexpected '!': expected '='");
                break;

            case ' ':
            case '\r':
            case '\t':
            case '\n':
                break;

            case '\'': {
                auto status = tokenize_string();
                if(!status.ok()) return status;
                break;
            }

            default:

                if(is_ascii_digit(ch)) {
                    auto status = tokenize_number();
                    if(!status.ok()) return status;
                    break;
                } else if(is_ascii_letter(ch) || ch == '_') {
                    auto status = tokenize_identifier_or_reserved_word();
                    if(!status.ok()) return status;
                    break;
                } else {
                    return make_lexer_error(start_location_, "unexpected character '"s+ch+"'");
                }

        }

        return core::Status::Ok();

    }

    core::Status Lexer::tokenize_identifier_or_reserved_word() {

        while(!is_at_end() && (is_ascii_alphanumeric(*peek_char()) || *peek_char() == '_')) {
            consume_char();
        }

        std::string lexeme = std::string{ source_.substr(start_pos_, current_pos_-start_pos_) };
        std::transform(lexeme.begin(), lexeme.end(), lexeme.begin(), to_ascii_upper);

        const auto reserved_word = RESERVED_WORD_KINDS.find(lexeme);
        
        if(reserved_word == RESERVED_WORD_KINDS.end()) {
            add_current_token(TokenKind::Identifier);
        } else {
            add_current_token(reserved_word->second);
        }

        return core::Status::Ok();

    }

    core::Status Lexer::tokenize_number() {

        while(!is_at_end() && is_ascii_digit(*peek_char())) {
            consume_char();
        }

        if(peek_char().has_value() && *peek_char() == '.') {

            consume_char();

            if(is_at_end() || !is_ascii_digit(*peek_char())) {
                return make_lexer_error(start_location_, "invalid double literal: expected a digit after decimal point");
            }

            while(!is_at_end() && is_ascii_digit(*peek_char())) {
                consume_char();
            }

            add_current_token(TokenKind::DoubleLiteral);

        } else {

            add_current_token(TokenKind::IntegerLiteral);

        }

        return core::Status::Ok();

    }

    core::Status Lexer::tokenize_string() {

        while(!is_at_end() && peek_char() != '\'') {
            consume_char();
        }

        if(is_at_end()) {
            return core::Status::IncompleteInput("unterminated string literal");
        }

        consume_char();

        std::string_view lexeme{ source_.substr(start_pos_+1, current_pos_-1-(start_pos_+1)) };
        add_token(TokenKind::StringLiteral, lexeme, start_location_);

        return core::Status::Ok();

    }

    bool Lexer::is_at_end() const {
        return current_pos_ >= source_.length();
    }

    char Lexer::consume_char() {

        if(source_[current_pos_] == '\n') {
            current_location_.line++;
            current_location_.column = 1;
        } else {
            current_location_.column++;
        }

        return source_[current_pos_++];

    }

    void Lexer::add_token(TokenKind kind, std::string_view lexeme, SourceLocation location) {
        tokens_.push_back(Token{ kind, std::string(lexeme), location });
    }

    void Lexer::add_current_token(TokenKind kind) {
        std::string lexeme{ source_.substr(start_pos_, current_pos_-start_pos_) };
        add_token(kind, lexeme, start_location_);
    }

    std::optional<char> Lexer::peek_char() const {
        if(is_at_end()) return std::nullopt;
        return source_[current_pos_];
    }

    std::optional<char> Lexer::peek_next_char() const {
        if(current_pos_+1 >= source_.length()) return std::nullopt;
        return source_[current_pos_+1];
    }

    bool Lexer::match_char(char expected) {

        if(is_at_end()) return false;
        if(source_[current_pos_] != expected) return false;

        consume_char();
        return true;

    }

}
