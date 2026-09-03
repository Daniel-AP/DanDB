#include <benchmark/benchmark.h>

#include "benchutil/Random.h"
#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::make_delete_by_primary_key_statement;
using dandb::benchutil::make_insert_statement;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_rows_affected;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    void benchmark_sql_delete_by_primary_key_unique_secondary_index(benchmark::State& state) {

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

        std::vector<std::size_t> delete_ids(entry_count);
        std::iota(delete_ids.begin(), delete_ids.end(), std::size_t{0});

        std::mt19937_64 random_engine(dandb::benchutil::BENCHMARK_RANDOM_SEED);
        std::shuffle(delete_ids.begin(), delete_ids.end(), random_engine);

        std::size_t delete_index = 0;

        for(auto _: state) {

            const auto delete_id = delete_ids[delete_index%entry_count];

            state.PauseTiming();
            const auto delete_statement = make_delete_by_primary_key_statement(delete_id);
            const auto restore_statement = make_insert_statement(delete_id);
            delete_index++;
            state.ResumeTiming();

            const auto delete_results = database.execute(delete_statement);
            const auto delete_status = verify_rows_affected(delete_results, 1);
            if(!delete_status.ok()) {
                state.SkipWithError(delete_status.message());
                return;
            }

            benchmark::DoNotOptimize(delete_results.size());

            state.PauseTiming();
            const auto restore_results = database.execute(restore_statement);
            const auto restore_status = verify_rows_affected(restore_results, 1);
            if(!restore_status.ok()) {
                state.SkipWithError(restore_status.message());
                return;
            }
            state.ResumeTiming();

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_delete_by_primary_key_unique_secondary_index)
    ->Name("SQL/DeleteByPrimaryKey/UniqueSecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
