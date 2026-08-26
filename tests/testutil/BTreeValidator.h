#pragma once

#include <dandb/btree/BTree.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Pager.h>

namespace dandb::testutil {

    core::Status validate_btree(
        storage::Pager& pager,
        const btree::BTree& tree
    );

}
