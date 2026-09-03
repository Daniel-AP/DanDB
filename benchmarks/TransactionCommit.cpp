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

    constexpr std::size_t COMMIT_TREE_ENTRY_COUNT = 100'000;

    void benchmark_transaction_commit(benchmark::State& state) {

        const auto update_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(COMMIT_TREE_ENTRY_COUNT);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        auto update_keys_result = make_shuffled_keys(COMMIT_TREE_ENTRY_COUNT);
        if(!update_keys_result.ok()) {
            state.SkipWithError(update_keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());
        const auto update_keys = std::move(update_keys_result.value());
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

        const auto populate_checkpoint_status = pager.checkpoint();
        if(!populate_checkpoint_status.ok()) {
            state.SkipWithError(populate_checkpoint_status.message());
            return;
        }

        for(auto _: state) {

            state.PauseTiming();

            const auto begin_update_transaction_status = pager.begin_transaction();
            if(!begin_update_transaction_status.ok()) {
                state.SkipWithError(begin_update_transaction_status.message());
                return;
            }

            for(std::size_t update_index = 0; update_index < update_count; update_index++) {

                const auto update_status = tree.update_value(update_keys[update_index], updated_value);
                if(!update_status.ok()) {
                    state.SkipWithError(update_status.message());
                    return;
                }

            }

            state.ResumeTiming();

            const auto commit_status = pager.commit_transaction();
            if(!commit_status.ok()) {
                state.SkipWithError(commit_status.message());
                return;
            }

            state.PauseTiming();

            const auto checkpoint_status = pager.checkpoint();
            if(!checkpoint_status.ok()) {
                state.SkipWithError(checkpoint_status.message());
                return;
            }

            state.ResumeTiming();

        }

        const auto close_status = pager.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations()*static_cast<std::int64_t>(update_count));
        state.counters["updates_per_commit"] = static_cast<double>(update_count);

    }

}

BENCHMARK(benchmark_transaction_commit)
    ->Name("Transaction/Commit")
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1'000)
    ->Unit(benchmark::kMicrosecond)
    ->UseRealTime();
