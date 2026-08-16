#include "Repl.h"

#include <dandb/core/Status.h>
#include <dandb/record/LogicalType.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace dandb::cli {

    namespace {

        constexpr std::string_view PRIMARY_PROMPT = "dandb> ";
        constexpr std::string_view CONTINUATION_PROMPT = "...> ";
        constexpr std::string_view HELP_COMMAND = ".help";
        constexpr std::string_view EXIT_COMMAND = ".exit";
        constexpr std::string_view PAGER_PROMPT = "--More-- Press Enter for more rows; enter text to stop: ";
        constexpr std::string_view HELP_TEXT =
            "Supported SQL: CREATE TABLE, DROP TABLE, INSERT, SELECT, UPDATE, "
            "DELETE, BEGIN, COMMIT, ROLLBACK, CHECKPOINT.\n"
            "End statements with ';'. Commands: .help, .exit.\n"
            "A blank continuation line submits the pending SQL.\n"
            "Results pause every 100 rows: Enter continues; any text skips the current "
            "result.\n";

        bool is_blank(std::string_view text) {

            for(const char character: text) {
                if(!std::isspace(static_cast<unsigned char>(character))) return false;
            }

            return true;

        }

        std::string escape_string(std::string_view text) {

            std::string escaped;

            for(const char character: text) {
                switch(character) {

                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    case '\r':
                        escaped += "\\r";
                        break;
                    case '\t':
                        escaped += "\\t";
                        break;
                    default:
                        escaped += character;
                        break;

                }
            }

            return escaped;

        }

    }

    Repl::Repl(execution::Database& database, std::istream& input, std::ostream& output, std::ostream& error_output) :
        database_(database),
        input_(input),
        output_(output),
        error_output_(error_output)
    {}

    void Repl::run() {

        std::string pending_input;

        while(true) {

            output_ << (pending_input.empty() ? PRIMARY_PROMPT : CONTINUATION_PROMPT) << std::flush;

            std::string line;
            if(!std::getline(input_, line)) return;

            if(pending_input.empty()) {

                if(is_blank(line)) continue;

                if(line == HELP_COMMAND) {
                    print_help();
                    continue;
                }

                if(line == EXIT_COMMAND) return;
            } else if(is_blank(line)) {

                const auto results = database_.execute(pending_input);
                pending_input.clear();

                if(!render_results(results)) return;
                continue;
            }

            pending_input += line;
            pending_input += '\n';

            const auto results = database_.execute(pending_input);

            const bool needs_more_input = (
                results.size() == 1 &&
                !results[0].status.ok() &&
                results[0].status.code() == core::StatusCode::IncompleteInput
            );
            if(needs_more_input) continue;

            pending_input.clear();

            if(!render_results(results)) return;
        }

    }

    bool Repl::render_results(const std::vector<execution::ExecutionResult>& results) {

        for(const auto& result: results) {

            if(!result.status.ok()) {
                error_output_ << result.status.message() << '\n';
                continue;
            }

            if(result.success_message.has_value()) output_ << *result.success_message << '\n';

            if(result.rows_affected.has_value()) {

                const std::size_t rows_affected = *result.rows_affected;

                output_ << rows_affected << " row";
                if(rows_affected != 1) output_ << "s";
                output_ << " affected\n";

            }

            if(result.row_set.has_value() && !render_row_set(*result.row_set)) return false;
        }

        return true;

    }

    bool Repl::render_row_set(const execution::ExecutionResult::RowSet& row_set) {

        for(std::size_t ordinal = 0; ordinal < row_set.column_names.size(); ordinal++) {
            if(ordinal != 0) output_ << ", ";

            output_ << row_set.column_names[ordinal];
        }
        output_ << '\n';

        std::size_t next_row = 0;
        bool skipped_rows = false;

        while(next_row < row_set.rows.size()) {

            const std::size_t page_end = std::min(next_row+PAGE_SIZE, row_set.rows.size());

            for(; next_row < page_end; next_row++) {
                print_row(row_set.rows[next_row]);
            }

            if(next_row == row_set.rows.size()) break;

            output_ << PAGER_PROMPT << std::flush;

            std::string pager_input;
            if(!std::getline(input_, pager_input)) return false;

            if(!pager_input.empty()) {
                skipped_rows = true;
                break;
            }
        }

        const std::size_t shown_rows = next_row;

        if(skipped_rows) {

            output_ << "(" << shown_rows << " of " << row_set.rows.size() << " rows shown)\n";
            return true;

        }

        output_ << "(" << row_set.rows.size() << " row";
        if(row_set.rows.size() != 1) output_ << "s";
        output_ << ")\n";

        return true;

    }

    void Repl::print_help() const {

        output_ << HELP_TEXT;

    }

    void Repl::print_row(const record::Row& row) const {

        for(std::size_t ordinal = 0; ordinal < row.value_count(); ordinal++) {
            if(ordinal != 0) output_ << ", ";

            output_ << format_value(row.value(ordinal));
        }
        output_ << '\n';

    }

    std::string Repl::format_value(const record::Value& value) {

        if(value.is_null()) return "NULL";

        switch(value.type().kind()) {

            case record::LogicalType::Kind::Int8:
            case record::LogicalType::Kind::Int16:
            case record::LogicalType::Kind::Int32:
            case record::LogicalType::Kind::Int64:
                return std::to_string(value.as_integer());

            case record::LogicalType::Kind::Float64: {

                std::ostringstream output;
                output << std::setprecision(std::numeric_limits<double>::max_digits10);
                output << value.as_float64();
                return output.str();

            }

            case record::LogicalType::Kind::String:
                return "\"" + escape_string(value.as_string()) + "\"";

            case record::LogicalType::Kind::Boolean:
                return value.as_boolean() ? "TRUE" : "FALSE";

        }

        return "NULL";

    }

}
