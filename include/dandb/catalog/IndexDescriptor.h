#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/IndexId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <string>

namespace dandb::catalog {

    class IndexDescriptor {
        public:
            static core::Result<IndexDescriptor> create(
                IndexId index_id,
                TableId table_id,
                std::string name,
                storage::PageId root_page_id,
                bool unique,
                bool primary,
                bool internal,
                ColumnId indexed_column_id
            );

            IndexId index_id() const;
            TableId table_id() const;
            const std::string& name() const;
            storage::PageId root_page_id() const;
            bool unique() const;
            bool primary() const;
            bool internal() const;
            ColumnId indexed_column_id() const;

        private:
            IndexDescriptor(
                IndexId index_id,
                TableId table_id,
                std::string name,
                storage::PageId root_page_id,
                bool unique,
                bool primary,
                bool internal,
                ColumnId indexed_column_id
            );

            IndexId index_id_;
            TableId table_id_;
            std::string name_;
            storage::PageId root_page_id_;
            bool unique_;
            bool primary_;
            bool internal_;
            ColumnId indexed_column_id_;
    };

}
