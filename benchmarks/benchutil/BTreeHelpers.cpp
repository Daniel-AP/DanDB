#include "BTreeHelpers.h"

#include <dandb/btree/BTreeCursor.h>
#include <dandb/record/KeyCodec.h>

#include <algorithm>
#include <random>
#include <utility>

namespace dandb::benchutil {

    core::Result<std::vector<BTreeKey>> make_sequential_keys(std::size_t entry_count) {

        std::vector<BTreeKey> keys;
        keys.reserve(entry_count);

        for(std::size_t entry_index = 0; entry_index < entry_count; entry_index++) {

            auto key_result = record::KeyCodec::encode(record::Value::int64(static_cast<std::int64_t>(entry_index)));
            if(!key_result.ok()) return key_result.status();

            keys.push_back(std::move(key_result.value()));

        }

        return keys;

    }

    core::Result<std::vector<BTreeKey>> make_shuffled_keys(std::size_t entry_count) {

        auto keys_result = make_sequential_keys(entry_count);
        if(!keys_result.ok()) return keys_result.status();

        auto keys = std::move(keys_result.value());
        std::mt19937_64 random_engine(0xD14B7EEULL);
        std::shuffle(keys.begin(), keys.end(), random_engine);

        return keys;

    }

    core::Status populate_tree(btree::BTree& tree, const std::vector<BTreeKey>& keys, const BTreeValue& value) {

        for(const auto& key: keys) {

            const auto insert_status = tree.insert(key, value);
            if(!insert_status.ok()) return insert_status;

        }

        return core::Status::Ok();

    }

    core::Status verify_entry_count(btree::BTree& tree, std::size_t expected_count) {

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
            return core::Status::InternalError("BTree benchmark stored an unexpected number of entries");
        }

        return core::Status::Ok();

    }

}
