#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/core/Status.h>
#include <dandb/execution/Database.h>
#include <dandb/execution/ExecutionResult.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::core::Status;
using dandb::execution::Database;
using dandb::execution::ExecutionResult;

namespace {

    Status verify_full_table_scan_result(const std::vector<ExecutionResult>& results, std::size_t expected_row_count) {

        const auto results_status = verify_successful_results(results, 1);
        if(!results_status.ok()) return results_status;

        const auto& result = results.front();
        if(!result.row_set.has_value() || result.row_set->rows.size() != expected_row_count) {
            return Status::InternalError("SQL full-table scan benchmark returned an unexpected row count");
        }

        return Status::Ok();

    }

    void benchmark_sql_full_table_scan(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        const TempDir temp_dir;

        auto database_result = Database::open_or_create(temp_dir.database_path());
        if(!database_result.ok()) {
            state.SkipWithError(database_result.status().message());
            return;
        }

        auto database = std::move(database_result.value());
        const auto table_status = populate_benchmark_table(database, entry_count);
        if(!table_status.ok()) {
            state.SkipWithError(table_status.message());
            return;
        }

        const std::string full_scan_statement = "SELECT value FROM benchmark_rows;";

        for(auto _: state) {

            const auto full_scan_results = database.execute(full_scan_statement);
            const auto scan_status = verify_full_table_scan_result(full_scan_results, entry_count);
            if(!scan_status.ok()) {
                state.SkipWithError(scan_status.message());
                return;
            }

            benchmark::DoNotOptimize(full_scan_results.front().row_set->rows.size());

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations()*static_cast<std::int64_t>(entry_count));

    }

}

BENCHMARK(benchmark_sql_full_table_scan)
    ->Name("SQL/FullTableScan")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
