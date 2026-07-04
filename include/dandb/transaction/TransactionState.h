#pragma once

#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace dandb::transaction {

    enum class TransactionStatus {
        Inactive,
        Active,
        Failed,
        Unresolved
    };

    struct TransactionState {
        TransactionStatus status = TransactionStatus::Inactive;
        std::uint64_t transaction_id = 0;
        std::unordered_set<storage::PageId> dirty_page_ids;
        std::unordered_set<storage::PageId> new_page_ids;
        std::unordered_map<storage::PageId, storage::Page> original_pages;

        bool in_transaction() const;
        bool is_failed() const;
        bool is_unresolved() const;
        void clear();
    };

}
