#include <benchmark/benchmark.h>

#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::benchutil::TempDir;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    void benchmark_sql_create_unique_secondary_index(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));

        for(auto _: state) {

            state.PauseTiming();

            {

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

                state.ResumeTiming();

                const auto index_results = database.execute("CREATE UNIQUE INDEX benchmark_rows_by_value ON benchmark_rows(value);");
                const auto index_status = verify_successful_results(index_results, 1);
                if(!index_status.ok()) {
                    state.SkipWithError(index_status.message());
                    return;
                }

                benchmark::DoNotOptimize(index_results.size());

                state.PauseTiming();

                const auto close_status = database.close();
                if(!close_status.ok()) {
                    state.SkipWithError(close_status.message());
                    return;
                }

            }

            state.ResumeTiming();

        }

        state.SetItemsProcessed(state.iterations()*static_cast<std::int64_t>(entry_count));

    }

}

BENCHMARK(benchmark_sql_create_unique_secondary_index)
    ->Name("SQL/CreateUniqueSecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);
