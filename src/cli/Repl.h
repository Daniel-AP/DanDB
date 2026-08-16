#pragma once

#include <dandb/execution/Database.h>
#include <dandb/execution/ExecutionResult.h>
#include <dandb/record/Row.h>
#include <dandb/record/Value.h>

#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace dandb::cli {

    class Repl {
        public:
            Repl(execution::Database& database, std::istream& input, std::ostream& output, std::ostream& error_output);

            void run();

        private:
            static constexpr std::size_t PAGE_SIZE = 100;

            bool render_results(const std::vector<execution::ExecutionResult>& results);
            bool render_row_set(const execution::ExecutionResult::RowSet& row_set);

            void print_help() const;
            void print_row(const record::Row& row) const;

            static std::string format_value(const record::Value& value);

            execution::Database& database_;
            std::istream& input_;
            std::ostream& output_;
            std::ostream& error_output_;
    };

}
