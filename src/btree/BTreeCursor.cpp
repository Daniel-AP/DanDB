#include <dandb/btree/BTreeCursor.h>

#include <dandb/storage/Pager.h>
#include <dandb/storage/PageId.h>
#include <dandb/btree/BTreeLeafPage.h>

#include <utility>

namespace dandb::btree {

    BTreeCursor::BTreeCursor(
        storage::Pager& pager,
        storage::PageId current_leaf_page_id
    ) :
        pager_(&pager),
        current_leaf_page_id_(current_leaf_page_id),
        entry_index_(0)
    {}

    core::Result<std::optional<BTreeEntry>> BTreeCursor::next() {

        while(current_leaf_page_id_.is_valid()) {

            auto handle_result = pager_->get_page(current_leaf_page_id_);
            if(!handle_result.ok()) {
                return handle_result.status();
            }

            auto& handle = handle_result.value();
            const auto* page = handle.page();

            auto page_view_result = BTreeLeafPage<const std::byte>::open(page->data());
            if(!page_view_result.ok()) {
                return page_view_result.status();
            }

            auto& page_view = page_view_result.value();

            if(entry_index_ < page_view.key_count()) {

                const auto key_bytes_result = page_view.key_at(entry_index_);
                if(!key_bytes_result.ok()) {
                    return key_bytes_result.status();
                }

                const auto value_bytes_result = page_view.value_at(entry_index_);
                if(!value_bytes_result.ok()) {
                    return value_bytes_result.status();
                }

                BTreeEntry result;
                result.key = std::vector<std::byte>(key_bytes_result.value().begin(), key_bytes_result.value().end());
                result.value = std::vector<std::byte>(value_bytes_result.value().begin(), value_bytes_result.value().end());

                entry_index_++;

                return std::optional<BTreeEntry>{ std::move(result) };

            }

            current_leaf_page_id_ = page_view.next_leaf_page_id();
            entry_index_ = 0;
        }

        return std::optional<BTreeEntry>{};
        
    }

}
