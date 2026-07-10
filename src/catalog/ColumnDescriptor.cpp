#include <dandb/catalog/ColumnDescriptor.h>

#include <dandb/core/Status.h>

#include <utility>

namespace dandb::catalog {

    core::Result<ColumnDescriptor> ColumnDescriptor::create(
        ColumnId column_id,
        TableId table_id,
        std::string name,
        record::LogicalType logical_type,
        std::size_t ordinal,
        bool nullable,
        bool primary_key,
        bool unique
    ) {
        
        if(!column_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create column descriptor: column id cannot be invalid");
        }

        if(!table_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create column descriptor: table id cannot be invalid");
        }

        if(name.empty()) {
            return core::Status::InvalidArgument("Cannot create column descriptor: name cannot be empty");
        }

        if(primary_key && nullable) {
            return core::Status::InvalidArgument("Cannot create column descriptor: primary key column cannot be nullable");
        }

        if(primary_key && !unique) {
            return core::Status::InvalidArgument("Cannot create column descriptor: primary key column must be unique");
        }

        if(unique && nullable) {
            return core::Status::InvalidArgument("Cannot create column descriptor: unique column cannot be nullable");
        }

        if(primary_key && !logical_type.can_be_indexed()) {
            return core::Status::InvalidArgument("Cannot create column descriptor: primary key column type must be indexable");
        }

        if(unique && !logical_type.can_be_indexed()) {
            return core::Status::InvalidArgument("Cannot create column descriptor: unique column type must be indexable");
        }

        return ColumnDescriptor(
            column_id,
            table_id,
            std::move(name),
            logical_type,
            ordinal,
            nullable,
            primary_key,
            unique
        );

    }

    ColumnDescriptor::ColumnDescriptor(
        ColumnId column_id,
        TableId table_id,
        std::string name,
        record::LogicalType logical_type,
        std::size_t ordinal,
        bool nullable,
        bool primary_key,
        bool unique
    ) :
        column_id_(column_id),
        table_id_(table_id),
        name_(std::move(name)),
        logical_type_(logical_type),
        ordinal_(ordinal),
        nullable_(nullable),
        primary_key_(primary_key),
        unique_(unique)
    {}

    ColumnId ColumnDescriptor::column_id() const {
        return column_id_;
    }

    TableId ColumnDescriptor::table_id() const {
        return table_id_;
    }

    const std::string& ColumnDescriptor::name() const {
        return name_;
    }

    record::LogicalType ColumnDescriptor::logical_type() const {
        return logical_type_;
    }

    std::size_t ColumnDescriptor::ordinal() const {
        return ordinal_;
    }

    bool ColumnDescriptor::nullable() const {
        return nullable_;
    }

    bool ColumnDescriptor::primary_key() const {
        return primary_key_;
    }

    bool ColumnDescriptor::unique() const {
        return unique_;
    }

}
