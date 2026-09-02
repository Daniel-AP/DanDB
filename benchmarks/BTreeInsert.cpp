#include <benchmark/benchmark.h>

#include <dandb/btree/BTree.h>
#include <dandb/btree/BTreeCursor.h>
#include <dandb/core/Status.h>
#include <dandb/record/KeyCodec.h>
#include <dandb/storage/Pager.h>
#include <testutil/TempDir.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

using dandb::btree::BTree;
using dandb::core::Result;
using dandb::core::Status;
using dandb::record::KeyCodec;
using dandb::record::Value;
using dandb::storage::Pager;
using dandb::testutil::TempDir;

namespace {

    constexpr std::uint16_t KEY_SIZE = static_cast<std::uint16_t>(sizeof(std::uint64_t));
    constexpr std::uint16_t VALUE_SIZE = 32;
    constexpr std::size_t BUFFER_POOL_CAPACITY = 2'500;

    using BTreeKey = std::vector<std::byte>;
    using BTreeValue = std::array<std::byte, VALUE_SIZE>;

    Result<std::vector<BTreeKey>> make_sequential_keys(std::size_t entry_count) {

        std::vector<BTreeKey> keys;
        keys.reserve(entry_count);

        for(std::size_t entry_index = 0; entry_index < entry_count; entry_index++) {

            auto key_result = KeyCodec::encode(Value::int64(static_cast<std::int64_t>(entry_index)));
            if(!key_result.ok()) return key_result.status();

            keys.push_back(std::move(key_result.value()));

        }

        return keys;

    }

    Result<std::vector<BTreeKey>> make_shuffled_keys(std::size_t entry_count) {

        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) return keys_result.status();

        auto keys = std::move(keys_result.value());
        std::mt19937_64 random_engine(0xD14B7EEULL);
        std::shuffle(keys.begin(), keys.end(), random_engine);

        return keys;

    }

    Status verify_entry_count(BTree& tree, std::size_t expected_count) {

        auto cursor_result = tree.scan();
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
            return Status::InternalError("BTree insert benchmark stored an unexpected number of entries");
        }

        return Status::Ok();

    }

    void benchmark_btree_insert(benchmark::State& state, const std::vector<BTreeKey>& keys) {

        const BTreeValue value{};
        bool validate_first_iteration = true;

        for(auto _: state) {

            state.PauseTiming();

            {
                const TempDir temp_dir;
                auto pager_result = Pager::create(temp_dir.database_path(), BUFFER_POOL_CAPACITY);
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

                auto tree_result = BTree::create_new(pager, KEY_SIZE, VALUE_SIZE);
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
        const auto bytes_per_entry = static_cast<std::int64_t>(KEY_SIZE+VALUE_SIZE);

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
