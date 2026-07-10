#include <dandb/btree/BTreeValidator.h>

#include <dandb/btree/BTreeInternalPage.h>
#include <dandb/btree/BTreeLeafPage.h>
#include <dandb/core/Result.h>
#include <dandb/storage/Pager.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

        using ValidateSubtreeResult = core::Result<std::optional<std::vector<std::byte>>>;

        ValidateSubtreeResult validate_subtree(
            ValidationContext& context,
            storage::PageId page_id,
            storage::PageId expected_parent_page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        );

        ValidateSubtreeResult validate_leaf(
            ValidationContext& context,
            const BTreeLeafPage<const std::byte>& leaf_page,
            storage::PageId page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        ) {

            if(!is_root) {
                const auto minimum_key_count = static_cast<std::uint16_t>((leaf_page.capacity()+1)/2);
                if(leaf_page.key_count() < minimum_key_count) {
                    return core::Status::Corruption("Cannot validate B+ tree: non-root leaf page is underfull");
                }
            }

            std::optional<std::vector<std::byte>> previous_key;
            std::optional<std::vector<std::byte>> first_key;
            std::optional<std::vector<std::byte>> last_key;

            for(std::uint16_t key_index = 0; key_index < leaf_page.key_count(); key_index++) {

                auto key_result = leaf_page.key_at(key_index);
                if(!key_result.ok()) {
                    return key_result.status();
                }

                const auto key = key_result.value();

                if(previous_key.has_value()) {
                    if(std::memcmp(previous_key->data(), key.data(), context.key_size) >= 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: leaf page keys are not strictly sorted");
                    }
                }

                if(key_range.lower_bound.has_value()) {
                    if(std::memcmp(key.data(), key_range.lower_bound->data(), context.key_size) < 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: leaf page key is below lower bound");
                    }
                }

                if(key_range.upper_bound.has_value()) {
                    if(std::memcmp(key.data(), key_range.upper_bound->data(), context.key_size) >= 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: leaf page key is above upper bound");
                    }
                }

                std::vector<std::byte> key_copy{ key.begin(), key.end() };
                if(!first_key.has_value()) {
                    first_key = key_copy;
                }

                last_key = key_copy;
                previous_key = std::move(key_copy);

            }

            if(!context.state.expected_leaf_depth.has_value()) {
                context.state.expected_leaf_depth = depth;
            } else if(context.state.expected_leaf_depth.value() != depth) {
                return core::Status::Corruption("Cannot validate B+ tree: leaves are not all at the same depth");
            }

            const auto minimum_key = first_key;

            context.state.leaves.push_back(LeafInfo{
                page_id,
                leaf_page.previous_leaf_page_id(),
                leaf_page.next_leaf_page_id(),
                std::move(first_key),
                std::move(last_key),
                depth
            });

            return minimum_key;

        }

        ValidateSubtreeResult validate_internal(
            ValidationContext& context,
            const BTreeInternalPage<const std::byte>& internal_page,
            storage::PageId page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        ) {

            if(!is_root) {
                const auto minimum_key_count = static_cast<std::uint16_t>(internal_page.capacity()/2);
                if(internal_page.key_count() < minimum_key_count) {
                    return core::Status::Corruption("Cannot validate B+ tree: non-root internal page is underfull");
                }
            }

            if(!internal_page.first_child_page_id().is_valid()) {
                return core::Status::Corruption("Cannot validate B+ tree: internal page first child page id is invalid");
            }

            if(internal_page.key_count() == 0) {
                return core::Status::Corruption("Cannot validate B+ tree: internal page has no separator keys");
            }

            std::optional<std::vector<std::byte>> previous_key;
            std::vector<std::vector<std::byte>> separator_keys;
            separator_keys.reserve(internal_page.key_count());

            for(std::uint16_t key_index = 0; key_index < internal_page.key_count(); key_index++) {

                auto key_result = internal_page.key_at(key_index);
                if(!key_result.ok()) {
                    return key_result.status();
                }

                const auto key = key_result.value();

                if(previous_key.has_value()) {
                    if(std::memcmp(previous_key->data(), key.data(), context.key_size) >= 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: internal page keys are not strictly sorted");
                    }
                }

                if(key_range.lower_bound.has_value()) {
                    if(std::memcmp(key.data(), key_range.lower_bound->data(), context.key_size) < 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: internal page key is below lower bound");
                    }
                }

                if(key_range.upper_bound.has_value()) {
                    if(std::memcmp(key.data(), key_range.upper_bound->data(), context.key_size) >= 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: internal page key is above upper bound");
                    }
                }

                std::vector<std::byte> key_copy{ key.begin(), key.end() };
                previous_key = key_copy;
                separator_keys.push_back(std::move(key_copy));

            }

            KeyRange first_child_range{
                key_range.lower_bound,
                key_range.upper_bound
            };

            first_child_range.upper_bound = separator_keys[0];

            auto first_child_result = validate_subtree(
                context,
                internal_page.first_child_page_id(),
                page_id,
                first_child_range,
                depth+1,
                false
            );
            if(!first_child_result.ok()) {
                return first_child_result.status();
            }

            const auto minimum_key = first_child_result.value();
            if(!minimum_key.has_value()) {
                return core::Status::Corruption("Cannot validate B+ tree: internal page first child subtree has no keys");
            }

            for(std::uint16_t entry_index = 0; entry_index < internal_page.key_count(); entry_index++) {

                auto right_child_page_id_result = internal_page.right_child_page_id_at(entry_index);
                if(!right_child_page_id_result.ok()) {
                    return right_child_page_id_result.status();
                }

                const auto right_child_page_id = right_child_page_id_result.value();
                if(!right_child_page_id.is_valid()) {
                    return core::Status::Corruption("Cannot validate B+ tree: internal page right child page id is invalid");
                }

                KeyRange child_range;
                child_range.lower_bound = separator_keys[entry_index];

                if(static_cast<std::size_t>(entry_index)+1 < separator_keys.size()) {
                    child_range.upper_bound = separator_keys[static_cast<std::size_t>(entry_index)+1];
                } else {
                    child_range.upper_bound = key_range.upper_bound;
                }

                auto child_result = validate_subtree(
                    context,
                    right_child_page_id,
                    page_id,
                    child_range,
                    depth+1,
                    false
                );
                if(!child_result.ok()) {
                    return child_result.status();
                }

                const auto& child_minimum_key = child_result.value();
                if(!child_minimum_key.has_value()) {
                    return core::Status::Corruption("Cannot validate B+ tree: internal page right child subtree has no keys");
                }

                if(std::memcmp(separator_keys[entry_index].data(), child_minimum_key->data(), context.key_size) != 0) {
                    return core::Status::Corruption("Cannot validate B+ tree: separator key does not match right child subtree minimum");
                }

            }

            return minimum_key;

        }

        ValidateSubtreeResult validate_subtree(
            ValidationContext& context,
            storage::PageId page_id,
            storage::PageId expected_parent_page_id,
            const KeyRange& key_range,
            std::size_t depth,
            bool is_root
        ) {

            if(!page_id.is_valid()) {
                return core::Status::Corruption("Cannot validate B+ tree: subtree page id is invalid");
            }

            auto [_, inserted] = context.state.visited_page_ids.insert(page_id);
            if(!inserted) {
                return core::Status::Corruption("Cannot validate B+ tree: page is reachable more than once");
            }

            auto page_handle_result = context.pager.get_page(page_id);
            if(!page_handle_result.ok()) {
                return page_handle_result.status();
            }

            const auto* page = page_handle_result.value().page();
            auto page_view_result = BTreePage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            const auto& page_view = page_view_result.value();
            if(page_view.key_size() != context.key_size) {
                return core::Status::Corruption("Cannot validate B+ tree: page key size does not match tree");
            }

            if(page_view.value_size() != context.value_size) {
                return core::Status::Corruption("Cannot validate B+ tree: page value size does not match tree");
            }

            if(is_root) {
                if(!page_view.is_root()) {
                    return core::Status::Corruption("Cannot validate B+ tree: root page is not marked as root");
                }

                if(page_view.parent_page_id().is_valid()) {
                    return core::Status::Corruption("Cannot validate B+ tree: root page parent page id is valid");
                }
            } else {
                if(page_view.is_root()) {
                    return core::Status::Corruption("Cannot validate B+ tree: non-root page is marked as root");
                }

                if(page_view.parent_page_id() != expected_parent_page_id) {
                    return core::Status::Corruption("Cannot validate B+ tree: page parent page id does not match parent");
                }
            }

            if(page_view.kind() == BTreePageKind::Leaf) {

                auto leaf_page_result = BTreeLeafPage<const std::byte>::open(page->data());
                if(!leaf_page_result.ok()) {
                    return leaf_page_result.status();
                }

                return validate_leaf(
                    context,
                    leaf_page_result.value(),
                    page_id,
                    key_range,
                    depth,
                    is_root
                );
                
            }

            auto internal_page_result = BTreeInternalPage<const std::byte>::open(page->data());
            if(!internal_page_result.ok()) {
                return internal_page_result.status();
            }

            return validate_internal(
                context,
                internal_page_result.value(),
                page_id,
                key_range,
                depth,
                is_root
            );

        }
       
        core::Status validate_leaf_links(const ValidationState& state) {

            if(state.leaves.empty()) {
                return core::Status::Corruption("Cannot validate B+ tree: tree has no leaves");
            }

            for(std::size_t leaf_index = 0; leaf_index < state.leaves.size(); leaf_index++) {

                const auto& leaf = state.leaves[leaf_index];

                const auto expected_previous_leaf_page_id = leaf_index == 0
                    ? storage::INVALID_PAGE_ID
                    : state.leaves[leaf_index-1].page_id;

                if(leaf.previous_leaf_page_id != expected_previous_leaf_page_id) {
                    return core::Status::Corruption("Cannot validate B+ tree: leaf previous link is inconsistent");
                }

                const auto expected_next_leaf_page_id = leaf_index+1 == state.leaves.size()
                    ? storage::INVALID_PAGE_ID
                    : state.leaves[leaf_index+1].page_id;

                if(leaf.next_leaf_page_id != expected_next_leaf_page_id) {
                    return core::Status::Corruption("Cannot validate B+ tree: leaf next link is inconsistent");
                }

                if(leaf_index+1 < state.leaves.size()) {
                    
                    const auto& next_leaf = state.leaves[leaf_index+1];

                    if(!leaf.last_key.has_value() || !next_leaf.first_key.has_value()) {
                        return core::Status::Corruption("Cannot validate B+ tree: non-root leaf page is empty");
                    }

                    if(std::memcmp(leaf.last_key->data(), next_leaf.first_key->data(), leaf.last_key->size()) >= 0) {
                        return core::Status::Corruption("Cannot validate B+ tree: leaf sibling keys are not strictly sorted");
                    }

                }

            }

            return core::Status::Ok();

        }

    }

    core::Status BTreeValidator::validate(
        storage::Pager& pager,
        storage::PageId root_page_id,
        std::uint16_t key_size,
        std::uint16_t value_size
    ) {

        if(!root_page_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot validate B+ tree: root page id is invalid");
        }

        if(key_size == 0) {
            return core::Status::InvalidArgument("Cannot validate B+ tree: key size must be greater than 0");
        }

        if(value_size == 0) {
            return core::Status::InvalidArgument("Cannot validate B+ tree: value size must be greater than 0");
        }

        ValidationState state;
        ValidationContext context{
            pager,
            key_size,
            value_size,
            state
        };

        const KeyRange root_key_range;
        auto subtree_result = validate_subtree(
            context,
            root_page_id,
            storage::INVALID_PAGE_ID,
            root_key_range,
            0,
            true
        );
        if(!subtree_result.ok()) {
            return subtree_result.status();
        }

        return validate_leaf_links(state);

    }

}
