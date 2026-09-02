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

    void benchmark_btree_random_erase(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        auto erase_keys_result = make_shuffled_keys(entry_count);
        if(!erase_keys_result.ok()) {
            state.SkipWithError(erase_keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());
        const auto erase_keys = std::move(erase_keys_result.value());
        const BTreeValue value{};

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
                const auto populate_status = populate_tree(tree, keys, value);
                if(!populate_status.ok()) {
                    state.SkipWithError(populate_status.message());
                    return;
                }

                const auto populate_commit_status = pager.commit_transaction();
                if(!populate_commit_status.ok()) {
                    state.SkipWithError(populate_commit_status.message());
                    return;
                }

                const auto begin_erase_transaction_status = pager.begin_transaction();
                if(!begin_erase_transaction_status.ok()) {
                    state.SkipWithError(begin_erase_transaction_status.message());
                    return;
                }

                state.ResumeTiming();

                for(const auto& key: erase_keys) {

                    const auto erase_status = tree.erase(key);
                    if(!erase_status.ok()) {
                        state.SkipWithError(erase_status.message());
                        return;
                    }

                }

                state.PauseTiming();

                const auto erase_commit_status = pager.commit_transaction();
                if(!erase_commit_status.ok()) {
                    state.SkipWithError(erase_commit_status.message());
                    return;
                }

                const auto close_status = pager.close();
                if(!close_status.ok()) {
                    state.SkipWithError(close_status.message());
                    return;
                }
            }

            state.ResumeTiming();

        }

        const auto entries_per_iteration = static_cast<std::int64_t>(erase_keys.size());
        const auto bytes_per_entry = static_cast<std::int64_t>(BTREE_KEY_SIZE+BTREE_VALUE_SIZE);

        state.SetItemsProcessed(state.iterations()*entries_per_iteration);
        state.SetBytesProcessed(state.iterations()*entries_per_iteration*bytes_per_entry);

    }

}

BENCHMARK(benchmark_btree_random_erase)
    ->Name("BTree/RandomErase")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);
