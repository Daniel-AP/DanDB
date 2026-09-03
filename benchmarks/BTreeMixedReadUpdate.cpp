#include <benchmark/benchmark.h>

#include "benchutil/BTreeHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/btree/BTree.h>
#include <dandb/storage/Pager.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::btree::BTree;
using dandb::benchutil::BTREE_BUFFER_POOL_CAPACITY;
using dandb::benchutil::BTREE_KEY_SIZE;
using dandb::benchutil::BTREE_VALUE_SIZE;
using dandb::benchutil::BTreeValue;
using dandb::benchutil::make_sequential_keys;
using dandb::benchutil::make_shuffled_keys;
using dandb::benchutil::populate_tree;
using dandb::benchutil::TempDir;
using dandb::storage::Pager;

namespace {

    void benchmark_btree_mixed_read_update(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        auto operation_keys_result = make_shuffled_keys(entry_count);
        if(!operation_keys_result.ok()) {
            state.SkipWithError(operation_keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());
        const auto operation_keys = std::move(operation_keys_result.value());
        const BTreeValue initial_value{};
        BTreeValue updated_value{};
        updated_value.fill(std::byte{ 0xFF });
        const TempDir temp_dir;

        auto pager_result = Pager::create(temp_dir.database_path(), BTREE_BUFFER_POOL_CAPACITY);
        if(!pager_result.ok()) {
            state.SkipWithError(pager_result.status().message());
            return;
        }

        auto pager = std::move(pager_result.value());
        const auto begin_populate_transaction_status = pager.begin_transaction();
        if(!begin_populate_transaction_status.ok()) {
            state.SkipWithError(begin_populate_transaction_status.message());
            return;
        }

        auto tree_result = BTree::create_new(pager, BTREE_KEY_SIZE, BTREE_VALUE_SIZE);
        if(!tree_result.ok()) {
            state.SkipWithError(tree_result.status().message());
            return;
        }

        auto tree = std::move(tree_result.value());
        const auto populate_status = populate_tree(tree, keys, initial_value);
        if(!populate_status.ok()) {
            state.SkipWithError(populate_status.message());
            return;
        }

        const auto populate_commit_status = pager.commit_transaction();
        if(!populate_commit_status.ok()) {
            state.SkipWithError(populate_commit_status.message());
            return;
        }

        const auto begin_workload_transaction_status = pager.begin_transaction();
        if(!begin_workload_transaction_status.ok()) {
            state.SkipWithError(begin_workload_transaction_status.message());
            return;
        }

        for(auto _: state) {

            for(std::size_t operation_index = 0; operation_index < operation_keys.size(); operation_index++) {

                const auto& key = operation_keys[operation_index];

                if(operation_index%5 == 0) {

                    const auto update_status = tree.update_value(key, updated_value);
                    if(!update_status.ok()) {
                        state.SkipWithError(update_status.message());
                        return;
                    }

                    continue;

                }

                auto lookup_result = tree.find(key);
                if(!lookup_result.ok()) {
                    state.SkipWithError(lookup_result.status().message());
                    return;
                }

                benchmark::DoNotOptimize(lookup_result.value().front());

            }

        }

        const auto workload_commit_status = pager.commit_transaction();
        if(!workload_commit_status.ok()) {
            state.SkipWithError(workload_commit_status.message());
            return;
        }

        const auto close_status = pager.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        const auto operations_per_iteration = static_cast<std::int64_t>(operation_keys.size());

        state.SetItemsProcessed(state.iterations()*operations_per_iteration);
        state.counters["read_ratio"] = 0.8;
        state.counters["update_ratio"] = 0.2;

    }

}

BENCHMARK(benchmark_btree_mixed_read_update)
    ->Name("BTree/MixedReadUpdate")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);
