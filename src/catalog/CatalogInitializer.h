#pragma once

#include <dandb/btree/BTree.h>
#include <dandb/catalog/ColumnId.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>

#include <array>

namespace dandb::storage {
    class Pager;
}

namespace dandb::catalog {

    class CatalogInitializer {
        public:
            explicit CatalogInitializer(storage::Pager& pager);

            core::Status initialize();

        private:
            struct InitializationState {
                record::Schema tables_schema;
                record::Schema columns_schema;
                record::Schema indexes_schema;
                record::Schema index_columns_schema;
                btree::BTree tables_tree;
                btree::BTree columns_tree;
                btree::BTree indexes_tree;
                btree::BTree index_columns_tree;
                std::array<ColumnId, 4> primary_key_column_ids;
            };

            core::Result<InitializationState> create_system_tables();
            core::Status insert_column_metadata(InitializationState& state);
            core::Status insert_table_metadata(InitializationState& state);
            core::Status insert_index_metadata(InitializationState& state);
            core::Status insert_index_column_metadata(InitializationState& state);
            static core::Status insert_row(
                btree::BTree& tree,
                const record::Schema& schema,
                const record::Row& row
            );

            storage::Pager& pager_;
    };

}
