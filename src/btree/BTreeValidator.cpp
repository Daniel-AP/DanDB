#include <dandb/btree/BTreeValidator.h>

#include <dandb/btree/BTreeInternalPage.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/storage/Pager.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_set>

namespace dandb::btree {
    namespace {

        struct KeyRange {
            std::optional<std::vector<std::byte>> lower_bound;
            std::optional<std::vector<std::byte>> upper_bound;
        };

        struct LeafInfo {
            storage::PageId page_id;
            storage::PageId previous_leaf_page_id;
            storage::PageId next_leaf_page_id;
            std::optional<std::vector<std::byte>> first_key;
            std::optional<std::vector<std::byte>> last_key;
            std::size_t depth;
        };

        struct ValidationState {
            std::unordered_set<storage::PageId> visited_page_ids;
            std::vector<LeafInfo> leaves;
            std::optional<std::size_t> expected_leaf_depth;
        };

        struct ValidationContext {
            storage::Pager& pager;
            std::uint16_t key_size;
            std::uint16_t value_size;
            ValidationState& state;
        };

        core::Status validate_subtree(
            ValidationContext& context,
            storage::PageId page_id,
            storage::PageId expected_parent_page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        );

        core::Status validate_leaf(
            ValidationContext& context,
            const BTreeLeafPage<const std::byte>& leaf_page,
            storage::PageId page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        );

        core::Status validate_internal(
            ValidationContext& context,
            const BTreeInternalPage<const std::byte>& internal_page,
            storage::PageId page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        );

        core::Status validate_leaf_links(const ValidationState& state);

    }
}
