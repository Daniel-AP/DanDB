#include <benchmark/benchmark.h>

#include "benchutil/Random.h"
#include "benchutil/SqlHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/execution/Database.h>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

using dandb::benchutil::TempDir;
using dandb::benchutil::make_point_lookup_statement;
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_point_lookup_result;
using dandb::benchutil::verify_successful_results;
using dandb::execution::Database;

namespace {

    void benchmark_sql_point_lookup_secondary_index(benchmark::State& state) {

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

        const auto index_results = database.execute("CREATE INDEX benchmark_rows_by_value ON benchmark_rows(value);");
        const auto index_status = verify_successful_results(index_results, 1);
        if(!index_status.ok()) {
            state.SkipWithError(index_status.message());
            return;
        }

        std::vector<std::size_t> lookup_values(entry_count);
        std::iota(lookup_values.begin(), lookup_values.end(), std::size_t{ 0 });

        std::mt19937_64 random_engine(dandb::benchutil::BENCHMARK_RANDOM_SEED);
        std::shuffle(lookup_values.begin(), lookup_values.end(), random_engine);

        std::vector<std::string> lookup_statements;
        lookup_statements.reserve(entry_count);

        for(const auto lookup_value: lookup_values) {
            lookup_statements.push_back(make_point_lookup_statement(lookup_value));
        }

        std::size_t statement_index = 0;

        for(auto _: state) {

            const auto& statement = lookup_statements[statement_index];
            const auto lookup_results = database.execute(statement);
            const auto expected_entry_id = static_cast<std::int64_t>(lookup_values[statement_index]);
            const auto lookup_status = verify_point_lookup_result(lookup_results, expected_entry_id);
            if(!lookup_status.ok()) {
                state.SkipWithError(lookup_status.message());
                return;
            }

            benchmark::DoNotOptimize(lookup_results.front().row_set->rows.front().value(0).as_integer());

            statement_index = (statement_index+1)%lookup_statements.size();

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_point_lookup_secondary_index)
    ->Name("SQL/PointLookup/SecondaryIndex")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
