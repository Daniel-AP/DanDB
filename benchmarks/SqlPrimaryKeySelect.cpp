#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>
#include <dandb/execution/ExecutionResult.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::verify_successful_results;
using dandb::core::Status;
using dandb::execution::Database;
using dandb::execution::ExecutionResult;

namespace {

    constexpr std::size_t SQL_INSERT_BATCH_SIZE = 10;

    Status verify_select_result(const std::vector<ExecutionResult>& results, std::int64_t expected_value) {

        const auto results_status = verify_successful_results(results, 1);
        if(!results_status.ok()) return results_status;

        const auto& result = results.front();
        if(!result.row_set.has_value() || result.row_set->rows.size() != 1) {
            return Status::InternalError("SQL primary-key benchmark returned an unexpected row count");
        }

        const auto actual_value = result.row_set->rows.front().value(0).as_integer();
        if(actual_value != expected_value) {
            return Status::InternalError("SQL primary-key benchmark returned an unexpected value");
        }

        return Status::Ok();

    }

    std::string make_insert_batch(std::size_t first_entry_index, std::size_t entry_count) {

        std::string statements = "BEGIN;";

        for(std::size_t entry_offset = 0; entry_offset < entry_count; entry_offset++) {

            const auto entry_index = first_entry_index+entry_offset;
            const auto value = std::to_string(entry_index);
            statements += "INSERT INTO benchmark_rows VALUES ("+value+", "+value+");";

        }

        statements += "COMMIT;";
        return statements;

    }

    std::string make_select_statement(std::size_t entry_index) {

        return "SELECT value FROM benchmark_rows WHERE id = "+std::to_string(entry_index)+";";

    }

    void benchmark_sql_primary_key_select(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        const TempDir temp_dir;

        auto database_result = Database::open_or_create(temp_dir.database_path());
        if(!database_result.ok()) {
            state.SkipWithError(database_result.status().message());
            return;
        }

        auto database = std::move(database_result.value());
        const auto create_results = database.execute(
            "CREATE TABLE benchmark_rows ("
            "id INT64 PRIMARY KEY, "
            "value INT64 NOT NULL"
            ");"
        );
        const auto create_status = verify_successful_results(create_results, 1);
        if(!create_status.ok()) {
            state.SkipWithError(create_status.message());
            return;
        }

        for(std::size_t first_entry_index = 0; first_entry_index < entry_count; first_entry_index += SQL_INSERT_BATCH_SIZE) {

            const auto remaining_entry_count = entry_count-first_entry_index;
            const auto batch_entry_count = std::min(SQL_INSERT_BATCH_SIZE, remaining_entry_count);
            const auto insert_results = database.execute(make_insert_batch(first_entry_index, batch_entry_count));
            const auto expected_result_count = batch_entry_count+2;
            const auto insert_status = verify_successful_results(insert_results, expected_result_count);
            if(!insert_status.ok()) {

                const auto error_message = "SQL benchmark row load failed at entry "+std::to_string(first_entry_index)+": "+insert_status.message();
                state.SkipWithError(error_message);
                return;

            }

        }

        const auto checkpoint_results = database.execute("CHECKPOINT;");
        const auto checkpoint_status = verify_successful_results(checkpoint_results, 1);
        if(!checkpoint_status.ok()) {
            state.SkipWithError(checkpoint_status.message());
            return;
        }

        std::vector<std::size_t> lookup_ids(entry_count);
        std::iota(lookup_ids.begin(), lookup_ids.end(), std::size_t{ 0 });

        std::mt19937_64 random_engine(0x6E3A9D17ULL);
        std::shuffle(lookup_ids.begin(), lookup_ids.end(), random_engine);

        std::vector<std::string> select_statements;
        select_statements.reserve(entry_count);

        for(const auto lookup_id: lookup_ids) {
            select_statements.push_back(make_select_statement(lookup_id));
        }

        std::size_t statement_index = 0;

        for(auto _: state) {

            const auto& statement = select_statements[statement_index];
            const auto select_results = database.execute(statement);
            const auto expected_value = static_cast<std::int64_t>(lookup_ids[statement_index]);
            const auto select_status = verify_select_result(select_results, expected_value);
            if(!select_status.ok()) {
                state.SkipWithError(select_status.message());
                return;
            }

            benchmark::DoNotOptimize(select_results.front().row_set->rows.front().value(0).as_integer());

            statement_index++;
            if(statement_index == select_statements.size()) statement_index = 0;

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_primary_key_select)
    ->Name("SQL/PrimaryKeySelect")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
