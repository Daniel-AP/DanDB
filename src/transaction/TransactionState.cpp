#include <dandb/transaction/TransactionState.h>

namespace dandb::transaction {

    bool TransactionState::in_transaction() const {
        return status != TransactionStatus::Inactive;
    }

    bool TransactionState::is_failed() const {
        return status == TransactionStatus::Failed;
    }

    void TransactionState::clear() {
        status = TransactionStatus::Inactive;
        transaction_id = 0;
        dirty_page_ids.clear();
        original_pages.clear();
    }

}
