#include <benchmark/benchmark.h>

#include "benchutil/BTreeHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/btree/BTree.h>
#include <dandb/storage/Pager.h>

#include <cstdint>
#include <utility>
#include <vector>

using dandb::btree::BTree;
using dandb::benchutil::BTREE_BUFFER_POOL_CAPACITY;
using dandb::benchutil::BTREE_KEY_SIZE;
using dandb::benchutil::BTREE_VALUE_SIZE;
using dandb::benchutil::BTreeKey;
using dandb::benchutil::BTreeValue;
using dandb::benchutil::make_sequential_keys;
using dandb::benchutil::make_shuffled_keys;
using dandb::benchutil::TempDir;
using dandb::benchutil::verify_entry_count;
using dandb::storage::Pager;

namespace {

    void benchmark_btree_insert(benchmark::State& state, const std::vector<BTreeKey>& keys) {

        const BTreeValue value{};
        bool validate_first_iteration = true;

        for(auto _: state) {

            state.PauseTiming();

            {
                const TempDir temp_dir;
                auto pager_result = Pager::create(temp_dir.database_path(), BTREE_BUFFER_POOL_CAPACITY);
                if(!pager_result.ok()) {
                    state.SkipWithError(pager_result.status().message());
                    return;
                }

                auto pager = std::move(pager_result.value());
                const auto begin_transaction_status = pager.begin_transaction();
                if(!begin_transaction_status.ok()) {
                    state.SkipWithError(begin_transaction_status.message());
                    return;
                }

                auto tree_result = BTree::create_new(pager, BTREE_KEY_SIZE, BTREE_VALUE_SIZE);
                if(!tree_result.ok()) {
                    state.SkipWithError(tree_result.status().message());
                    return;
                }

                auto tree = std::move(tree_result.value());
                state.ResumeTiming();

                for(const auto& key: keys) {

                    const auto insert_status = tree.insert(key, value);
                    if(!insert_status.ok()) {
                        state.SkipWithError(insert_status.message());
                        return;
                    }

                }

                const auto commit_status = pager.commit_transaction();
                if(!commit_status.ok()) {
                    state.SkipWithError(commit_status.message());
                    return;
                }

                state.PauseTiming();

                if(validate_first_iteration) {

                    const auto verify_status = verify_entry_count(tree, keys.size());
                    if(!verify_status.ok()) {
                        state.SkipWithError(verify_status.message());
                        return;
                    }

                }

                const auto close_status = pager.close();
                if(!close_status.ok()) {
                    state.SkipWithError(close_status.message());
                    return;
                }

                validate_first_iteration = false;
            }

            state.ResumeTiming();

        }

        const auto entries_per_iteration = static_cast<std::int64_t>(keys.size());
        const auto bytes_per_entry = static_cast<std::int64_t>(BTREE_KEY_SIZE+BTREE_VALUE_SIZE);

        state.SetItemsProcessed(state.iterations()*entries_per_iteration);
        state.SetBytesProcessed(state.iterations()*entries_per_iteration*bytes_per_entry);

    }

    void benchmark_btree_sequential_insert(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());

        benchmark_btree_insert(state, keys);

    }

    void benchmark_btree_shuffled_insert(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_shuffled_keys(entry_count);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());

        benchmark_btree_insert(state, keys);

    }

}

BENCHMARK(benchmark_btree_sequential_insert)
    ->Name("BTree/SequentialInsert")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(benchmark_btree_shuffled_insert)
    ->Name("BTree/ShuffledInsert")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);
