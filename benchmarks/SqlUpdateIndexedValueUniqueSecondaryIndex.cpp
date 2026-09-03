#include <benchmark/benchmark.h>

#include "benchutil/Random.h"
#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::make_update_value_statement;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    void benchmark_sql_update_indexed_value_unique_secondary_index(benchmark::State& state) {

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

        const auto index_results = database.execute("CREATE UNIQUE INDEX benchmark_rows_by_value ON benchmark_rows(value);");
        const auto index_status = verify_successful_results(index_results, 1);
        if(!index_status.ok()) {
            state.SkipWithError(index_status.message());
            return;
        }

        std::vector<std::size_t> update_ids(entry_count);
        std::iota(update_ids.begin(), update_ids.end(), std::size_t{0});

        std::mt19937_64 random_engine(dandb::benchutil::BENCHMARK_RANDOM_SEED);
        std::shuffle(update_ids.begin(), update_ids.end(), random_engine);

        std::size_t update_index = 0;

        for(auto _: state) {

            const auto update_id = update_ids[update_index%entry_count];
            const auto update_round = update_index/entry_count;
            const auto new_value = entry_count+update_round*entry_count+update_id;

            state.PauseTiming();
            const auto update_statement = make_update_value_statement(update_id, new_value);
            update_index++;
            state.ResumeTiming();

            const auto update_results = database.execute(update_statement);
            const auto update_status = verify_successful_results(update_results, 1);
            if(!update_status.ok()) {
                state.SkipWithError(update_status.message());
                return;
            }

            benchmark::DoNotOptimize(update_results.size());

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_update_indexed_value_unique_secondary_index)
    ->Name("SQL/UpdateIndexedValue/UniqueSecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
