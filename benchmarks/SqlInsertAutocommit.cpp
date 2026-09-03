#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <utility>

using dandb::benchutil::TempDir;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    void benchmark_sql_insert_autocommit(benchmark::State& state) {

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

        std::size_t next_entry_index = entry_count;

        for(auto _: state) {

            state.PauseTiming();
            const auto insert_statement = dandb::benchutil::make_insert_statement(next_entry_index);
            next_entry_index++;
            state.ResumeTiming();

            const auto insert_results = database.execute(insert_statement);
            const auto insert_status = verify_successful_results(insert_results, 1);
            if(!insert_status.ok()) {
                state.SkipWithError(insert_status.message());
                return;
            }

            benchmark::DoNotOptimize(insert_results.size());

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_insert_autocommit)
    ->Name("SQL/InsertAutocommit")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
