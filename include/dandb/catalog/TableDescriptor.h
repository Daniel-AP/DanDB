#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Result.h>
#include <dandb/storage/PageId.h>

#include <string>

namespace dandb::catalog {

    class TableDescriptor {
        public:
            static core::Result<TableDescriptor> create(
                TableId table_id,
                std::string name,
                storage::PageId root_page_id,
                ColumnId primary_key_column_id
            );

            TableId table_id() const;
            const std::string& name() const;
            storage::PageId root_page_id() const;
            ColumnId primary_key_column_id() const;

        private:
            TableDescriptor(
                TableId table_id,
                std::string name,
                storage::PageId root_page_id,
                ColumnId primary_key_column_id
            );

            TableId table_id_;
            std::string name_;
            storage::PageId root_page_id_;
            ColumnId primary_key_column_id_;
    };

}
