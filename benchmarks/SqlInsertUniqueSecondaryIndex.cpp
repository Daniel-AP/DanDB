#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::benchutil::TempDir;
using dandb::benchutil::make_insert_batch;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    constexpr std::size_t INSERT_BATCH_SIZE = 100;

    void benchmark_sql_insert_unique_secondary_index(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        const auto expected_result_count = INSERT_BATCH_SIZE+2;
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

        const auto checkpoint_results = database.execute("CHECKPOINT;");
        const auto checkpoint_status = verify_successful_results(checkpoint_results, 1);
        if(!checkpoint_status.ok()) {
            state.SkipWithError(checkpoint_status.message());
            return;
        }

        std::size_t first_entry_index = entry_count;

        for(auto _: state) {

            state.PauseTiming();
            const auto insert_batch = make_insert_batch(first_entry_index, INSERT_BATCH_SIZE);
            first_entry_index += INSERT_BATCH_SIZE;
            state.ResumeTiming();

            const auto insert_results = database.execute(insert_batch);
            const auto insert_status = verify_successful_results(insert_results, expected_result_count);
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

        state.SetItemsProcessed(state.iterations()*static_cast<std::int64_t>(INSERT_BATCH_SIZE));

    }

}

BENCHMARK(benchmark_sql_insert_unique_secondary_index)
    ->Name("SQL/Insert/UniqueSecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
