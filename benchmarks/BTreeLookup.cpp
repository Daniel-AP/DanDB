#include <benchmark/benchmark.h>

#include "benchutil/BTreeHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/btree/BTree.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Pager.h>

#include <cstddef>
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
using dandb::benchutil::populate_tree;
using dandb::benchutil::TempDir;
using dandb::benchutil::verify_entry_count;
using dandb::core::Status;
using dandb::storage::Pager;

namespace {

    Status verify_lookup_value(BTree& tree, const BTreeKey& key, const BTreeValue& expected_value) {

        auto lookup_result = tree.find(key);
        if(!lookup_result.ok()) return lookup_result.status();

        const std::vector<std::byte> expected_value_bytes{ expected_value.begin(), expected_value.end() };
        if(lookup_result.value() != expected_value_bytes) {
            return Status::InternalError("BTree lookup benchmark returned an unexpected value");
        }

        return Status::Ok();

    }

    void benchmark_btree_random_lookup(benchmark::State& state) {

        const auto entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        auto lookup_keys_result = make_shuffled_keys(entry_count);
        if(!lookup_keys_result.ok()) {
            state.SkipWithError(lookup_keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());
        const auto lookup_keys = std::move(lookup_keys_result.value());
        const BTreeValue value{};
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
        const auto populate_status = populate_tree(tree, keys, value);
        if(!populate_status.ok()) {
            state.SkipWithError(populate_status.message());
            return;
        }

        const auto commit_status = pager.commit_transaction();
        if(!commit_status.ok()) {
            state.SkipWithError(commit_status.message());
            return;
        }

        const auto verify_count_status = verify_entry_count(tree, keys.size());
        if(!verify_count_status.ok()) {
            state.SkipWithError(verify_count_status.message());
            return;
        }

        const auto verify_value_status = verify_lookup_value(tree, lookup_keys.front(), value);
        if(!verify_value_status.ok()) {
            state.SkipWithError(verify_value_status.message());
            return;
        }

        std::size_t lookup_index = 0;

        for(auto _: state) {

            auto lookup_result = tree.find(lookup_keys[lookup_index]);
            if(!lookup_result.ok()) {
                state.SkipWithError(lookup_result.status().message());
                return;
            }

            benchmark::DoNotOptimize(lookup_result.value().front());

            lookup_index = (lookup_index+1)%lookup_keys.size();

        }

        const auto close_status = pager.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        state.SetItemsProcessed(state.iterations());

    }

}

BENCHMARK(benchmark_btree_random_lookup)
    ->Name("BTree/RandomLookup")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(100'000)
    ->Unit(benchmark::kNanosecond);
