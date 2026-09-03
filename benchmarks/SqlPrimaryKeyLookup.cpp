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
using dandb::benchutil::populate_benchmark_table;
using dandb::benchutil::verify_successful_results;
using dandb::core::Status;
using dandb::execution::Database;
using dandb::execution::ExecutionResult;

namespace {

    Status verify_lookup_result(const std::vector<ExecutionResult>& results, std::int64_t expected_value) {

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

    std::string make_lookup_statement(std::size_t entry_index) {

        return "SELECT value FROM benchmark_rows WHERE id = "+std::to_string(entry_index)+";";

    }

    void benchmark_sql_primary_key_lookup(benchmark::State& state) {

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

        std::vector<std::size_t> lookup_ids(entry_count);
        std::iota(lookup_ids.begin(), lookup_ids.end(), std::size_t{ 0 });

        std::mt19937_64 random_engine(0x6E3A9D17ULL);
        std::shuffle(lookup_ids.begin(), lookup_ids.end(), random_engine);

        std::vector<std::string> lookup_statements;
        lookup_statements.reserve(entry_count);

        for(const auto lookup_id: lookup_ids) {
            lookup_statements.push_back(make_lookup_statement(lookup_id));
        }

        std::size_t statement_index = 0;

        for(auto _: state) {

            const auto& statement = lookup_statements[statement_index];
            const auto lookup_results = database.execute(statement);
            const auto expected_value = static_cast<std::int64_t>(lookup_ids[statement_index]);
            const auto lookup_status = verify_lookup_result(lookup_results, expected_value);
            if(!lookup_status.ok()) {
                state.SkipWithError(lookup_status.message());
                return;
            }

            benchmark::DoNotOptimize(lookup_results.front().row_set->rows.front().value(0).as_integer());

            statement_index++;
            if(statement_index == lookup_statements.size()) statement_index = 0;

        }

        const auto close_status = database.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_sql_primary_key_lookup)
    ->Name("SQL/PrimaryKeyLookup")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMicrosecond);
