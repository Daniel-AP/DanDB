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
        auto status = validate_subtree(
            context,
            root_page_id,
            storage::INVALID_PAGE_ID,
            root_key_range,
            0,
            true
        );
        if(!status.ok()) {
            return status;
        }

        return validate_leaf_links(state);

    }

}
