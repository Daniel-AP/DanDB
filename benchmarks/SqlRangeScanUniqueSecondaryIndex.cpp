#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::benchutil::TempDir;
using dandb::benchutil::make_value_range_scan_statement;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_range_scan_result;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    constexpr std::size_t RANGE_ENTRY_COUNT = 100;

    void benchmark_sql_range_scan_unique_secondary_index(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        const auto first_value = entry_count-RANGE_ENTRY_COUNT;
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

        const auto index_results = database.execute("CREATE UNIQUE INDEX benchmark_rows_by_value ON benchmark_rows(value);");
        const auto index_status = verify_successful_results(index_results, 1);
        if(!index_status.ok()) {
            state.SkipWithError(index_status.message());
            return;
        }

        const auto range_scan_statement = make_value_range_scan_statement(first_value);

        for(auto _: state) {

            const auto range_scan_results = database.execute(range_scan_statement);
            const auto scan_status = verify_range_scan_result(range_scan_results, RANGE_ENTRY_COUNT);
            if(!scan_status.ok()) {
                state.SkipWithError(scan_status.message());
                return;
            }

            benchmark::DoNotOptimize(range_scan_results.front().row_set->rows.front().value(0).as_integer());

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations()*static_cast<std::int64_t>(RANGE_ENTRY_COUNT));

    }

}

BENCHMARK(benchmark_sql_range_scan_unique_secondary_index)
    ->Name("SQL/RangeScan/UniqueSecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
