#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <dandb/execution/ExecutionResult.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::verify_successful_results;
using dandb::core::Status;
using dandb::execution::Database;
using dandb::execution::ExecutionResult;

namespace {

    constexpr std::size_t SECONDARY_INDEX_CATEGORY_COUNT = 100;
    constexpr std::size_t SQL_INSERT_BATCH_SIZE = 10;

    std::string make_secondary_index_insert_batch(std::size_t first_entry_index, std::size_t entry_count) {

        std::string statements = "BEGIN;";

        for(std::size_t entry_offset = 0; entry_offset < entry_count; entry_offset++) {

            const auto entry_index = first_entry_index+entry_offset;
            const auto category = entry_index%SECONDARY_INDEX_CATEGORY_COUNT;
            const auto entry_value = std::to_string(entry_index);
            statements += "INSERT INTO benchmark_rows VALUES ("+entry_value+", "+std::to_string(category)+", "+entry_value+");";

        }

        statements += "COMMIT;";
        return statements;

    }

    Status verify_secondary_index_scan_result(
        const std::vector<ExecutionResult>& results,
        std::size_t entry_count,
        std::size_t category
    ) {

        const auto results_status = verify_successful_results(results, 1);
        if(!results_status.ok()) return results_status;

        const auto expected_row_count = entry_count/SECONDARY_INDEX_CATEGORY_COUNT;
        const auto& result = results.front();
        if(!result.row_set.has_value() || result.row_set->rows.size() != expected_row_count) {
            return Status::InternalError("SQL secondary-index benchmark returned an unexpected row count");
        }

        return Status::Ok();

    }

    std::string make_secondary_index_select_statement(std::size_t category) {

        return "SELECT id FROM benchmark_rows WHERE category = "+std::to_string(category)+";";

    }

    void benchmark_sql_secondary_index_scan(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        const TempDir temp_dir;

        auto database_result = Database::open_or_create(temp_dir.database_path());
        if(!database_result.ok()) {
            state.SkipWithError(database_result.status().message());
            return;
        }

        auto database = std::move(database_result.value());
        const auto setup_results = database.execute(
            "CREATE TABLE benchmark_rows ("
            "id INT64 PRIMARY KEY, "
            "category INT64 NOT NULL, "
            "value INT64 NOT NULL"
            ");"
            "CREATE INDEX benchmark_rows_by_category ON benchmark_rows(category);"
        );
        const auto setup_status = verify_successful_results(setup_results, 2);
        if(!setup_status.ok()) {
            state.SkipWithError(setup_status.message());
            return;
        }

        for(std::size_t first_entry_index = 0; first_entry_index < entry_count; first_entry_index += SQL_INSERT_BATCH_SIZE) {

            const auto remaining_entry_count = entry_count-first_entry_index;
            const auto batch_entry_count = std::min(SQL_INSERT_BATCH_SIZE, remaining_entry_count);
            const auto insert_results = database.execute(make_secondary_index_insert_batch(first_entry_index, batch_entry_count));
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

        std::vector<std::string> select_statements;
        select_statements.reserve(SECONDARY_INDEX_CATEGORY_COUNT);

        for(std::size_t category = 0; category < SECONDARY_INDEX_CATEGORY_COUNT; category++) {
            select_statements.push_back(make_secondary_index_select_statement(category));
        }

        std::size_t statement_index = 0;

        for(auto _: state) {

            const auto& statement = select_statements[statement_index];
            const auto select_results = database.execute(statement);
            const auto select_status = verify_secondary_index_scan_result(select_results, entry_count, statement_index);
            if(!select_status.ok()) {
                state.SkipWithError(select_status.message());
                return;
            }

            benchmark::DoNotOptimize(select_results.front().row_set->rows.size());

            statement_index++;
            if(statement_index == select_statements.size()) statement_index = 0;

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        const auto matching_row_count = static_cast<std::int64_t>(entry_count/SECONDARY_INDEX_CATEGORY_COUNT);
        state.SetItemsProcessed(state.iterations()*matching_row_count);

    }

}

BENCHMARK(benchmark_sql_secondary_index_scan)
    ->Name("SQL/SecondaryIndexScan")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
