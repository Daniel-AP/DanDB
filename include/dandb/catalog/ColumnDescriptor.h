#pragma once

#include <dandb/catalog/ColumnId.h>
#include <dandb/catalog/TableId.h>
#include <dandb/core/Result.h>
#include <dandb/record/LogicalType.h>

#include <cstddef>
#include <string>

namespace dandb::catalog {

    class ColumnDescriptor {
        public:
            static core::Result<ColumnDescriptor> create(
                ColumnId column_id,
                TableId table_id,
                std::string name,
                record::LogicalType logical_type,
                std::size_t ordinal,
                bool nullable,
                bool primary_key,
                bool unique
            );

            ColumnId column_id() const;
            TableId table_id() const;
            const std::string& name() const;
            record::LogicalType logical_type() const;
            std::size_t ordinal() const;
            bool nullable() const;
            bool primary_key() const;
            bool unique() const;

        private:
            ColumnDescriptor(
                ColumnId column_id,
                TableId table_id,
                std::string name,
                record::LogicalType logical_type,
                std::size_t ordinal,
                bool nullable,
                bool primary_key,
                bool unique
            );

            ColumnId column_id_;
            TableId table_id_;
            std::string name_;
            record::LogicalType logical_type_;
            std::size_t ordinal_;
            bool nullable_;
            bool primary_key_;
            bool unique_;
    };

}
