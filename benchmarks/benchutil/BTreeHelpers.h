#pragma once

#include <dandb/btree/BTree.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dandb::benchutil {

    constexpr std::uint16_t BTREE_KEY_SIZE = static_cast<std::uint16_t>(sizeof(std::uint64_t));
    constexpr std::uint16_t BTREE_VALUE_SIZE = 32;
    // A transaction keeps dirty pages resident until commit
    constexpr std::size_t BTREE_BUFFER_POOL_CAPACITY = 2'500;

    using BTreeKey = std::vector<std::byte>;
    using BTreeValue = std::array<std::byte, BTREE_VALUE_SIZE>;

    core::Result<std::vector<BTreeKey>> make_sequential_keys(std::size_t entry_count);
    core::Result<std::vector<BTreeKey>> make_shuffled_keys(std::size_t entry_count);
    core::Status populate_tree(btree::BTree& tree, const std::vector<BTreeKey>& keys, const BTreeValue& value);
    core::Status verify_entry_count(btree::BTree& tree, std::size_t expected_count);

}
