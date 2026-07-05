#pragma once

#include <dandb/record/LogicalType.h>
#include <dandb/core/Result.h>

#include <string>
#include <cstddef>

namespace dandb::record {

    class Schema;

    class Column {
        public:
            static core::Result<Column> create(
                std::string name,
                LogicalType logical_type,
                bool nullable,
                bool pk,
                bool unique
            );

            const std::string& name() const;
            LogicalType logical_type() const;
            bool nullable() const;
            bool pk() const;
            bool unique() const;
            std::size_t ordinal() const;
            std::size_t fixed_offset() const;

        private:
            friend class Schema;

            Column(
                std::string name,
                LogicalType logical_type,
                bool nullable,
                bool pk,
                bool unique
            );

            void set_layout(std::size_t ordinal, std::size_t fixed_offset);

            std::string name_;
            LogicalType logical_type_;
            bool nullable_;
            bool pk_;
            bool unique_;
            std::size_t ordinal_ = 0;
            std::size_t fixed_offset_ = 0;
    };

}