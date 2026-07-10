#include <dandb/btree/BTreeCursor.h>

#include <dandb/storage/Pager.h>
#include <dandb/storage/PageId.h>
#include <dandb/btree/BTreeLeafPage.h>

#include <cstring>
#include <utility>

namespace dandb::btree {

    BTreeCursor::BTreeCursor(
        storage::Pager& pager,
        storage::PageId current_leaf_page_id,
        std::uint16_t entry_index,
        std::optional<std::span<const std::byte>> upper_bound
    ) :
        pager_(&pager),
        current_leaf_page_id_(current_leaf_page_id),
        entry_index_(entry_index)
    {
        if(upper_bound.has_value()) {
            upper_bound_ = std::vector<std::byte>{ (*upper_bound).begin(), (*upper_bound).end() };
        }
    }

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

                const auto key_bytes = key_bytes_result.value();
                const bool has_reached_upper_bound = upper_bound_.has_value()
                    && std::memcmp(key_bytes.data(), (*upper_bound_).data(), (*upper_bound_).size()) >= 0;
                if(has_reached_upper_bound) {
                    current_leaf_page_id_ = storage::INVALID_PAGE_ID;
                    return std::optional<BTreeEntry>{};
                }

                const auto value_bytes_result = page_view.value_at(entry_index_);
                if(!value_bytes_result.ok()) {
                    return value_bytes_result.status();
                }

                BTreeEntry result;
                result.key = std::vector<std::byte>(key_bytes.begin(), key_bytes.end());
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
