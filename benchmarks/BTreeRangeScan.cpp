#include <benchmark/benchmark.h>

#include "benchutil/BTreeHelpers.h"
#include "benchutil/TempDir.h"

#include <dandb/btree/BTree.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Pager.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::btree::BTree;
using dandb::benchutil::BTREE_BUFFER_POOL_CAPACITY;
using dandb::benchutil::BTREE_KEY_SIZE;
using dandb::benchutil::BTREE_VALUE_SIZE;
using dandb::benchutil::BTreeKey;
using dandb::benchutil::BTreeValue;
using dandb::benchutil::make_sequential_keys;
using dandb::benchutil::populate_tree;
using dandb::benchutil::TempDir;
using dandb::benchutil::verify_entry_count;
using dandb::core::Status;
using dandb::storage::Pager;

namespace {

    constexpr std::size_t RANGE_SCAN_TREE_ENTRY_COUNT = 100'000;
    constexpr std::size_t RANGE_SCAN_START_INDEX = 25'000;

    Status verify_range_entry_count(
        BTree& tree,
        const BTreeKey& lower_bound,
        const BTreeKey& upper_bound,
        std::size_t expected_count
    ) {

        auto cursor_result = tree.scan_range(lower_bound, upper_bound);
        if(!cursor_result.ok()) return cursor_result.status();

        auto cursor = std::move(cursor_result.value());
        std::size_t actual_count = 0;

        while(true) {

            auto entry_result = cursor.next();
            if(!entry_result.ok()) return entry_result.status();
            if(!entry_result.value().has_value()) break;

            actual_count++;

        }

        if(actual_count != expected_count) {
            return Status::InternalError("BTree range scan benchmark returned an unexpected number of entries");
        }

        return Status::Ok();

    }

    void benchmark_btree_range_scan(benchmark::State& state) {

        const auto range_entry_count = static_cast<std::size_t>(state.range(0));
        auto keys_result = make_sequential_keys(RANGE_SCAN_TREE_ENTRY_COUNT);
        if(!keys_result.ok()) {
            state.SkipWithError(keys_result.status().message());
            return;
        }

        const auto keys = std::move(keys_result.value());
        const auto lower_bound = keys[RANGE_SCAN_START_INDEX];
        const auto upper_bound = keys[RANGE_SCAN_START_INDEX+range_entry_count];
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

        const auto verify_range_status = verify_range_entry_count(tree, lower_bound, upper_bound, range_entry_count);
        if(!verify_range_status.ok()) {
            state.SkipWithError(verify_range_status.message());
            return;
        }

        for(auto _: state) {

            auto cursor_result = tree.scan_range(lower_bound, upper_bound);
            if(!cursor_result.ok()) {
                state.SkipWithError(cursor_result.status().message());
                return;
            }

            auto cursor = std::move(cursor_result.value());

            while(true) {

                auto entry_result = cursor.next();
                if(!entry_result.ok()) {
                    state.SkipWithError(entry_result.status().message());
                    return;
                }
                if(!entry_result.value().has_value()) break;

                benchmark::DoNotOptimize(entry_result.value()->value.front());

            }

        }

        const auto close_status = pager.close();
        if(!close_status.ok()) {
            state.SkipWithError(close_status.message());
            return;
        }

        const auto entries_per_iteration = static_cast<std::int64_t>(range_entry_count);
        const auto bytes_per_entry = static_cast<std::int64_t>(BTREE_KEY_SIZE+BTREE_VALUE_SIZE);

        state.SetItemsProcessed(state.iterations()*entries_per_iteration);
        state.SetBytesProcessed(state.iterations()*entries_per_iteration*bytes_per_entry);

    }

}

BENCHMARK(benchmark_btree_range_scan)
    ->Name("BTree/RangeScan")
    ->Arg(1'000)
    ->Arg(10'000)
    ->Arg(50'000)
    ->Unit(benchmark::kMicrosecond);
