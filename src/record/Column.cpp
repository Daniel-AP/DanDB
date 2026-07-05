#include <dandb/record/Column.h>

#include <dandb/core/Status.h>

#include <string>
#include <utility>

namespace dandb::record {

    core::Result<Column> Column::create(
        std::string name,
        LogicalType logical_type,
        bool nullable,
        bool pk,
        bool unique
    ) {
        
        if(name.empty()) {
            return core::Status::InvalidArgument("Cannot create column: name cannot be empty");
        }

        if(pk && nullable) {
            return core::Status::InvalidArgument("Cannot create column: primary key column cannot be nullable");
        }

        if(unique && nullable) {
            return core::Status::InvalidArgument("Cannot create column: unique column cannot be nullable");
        }

        if(pk && !logical_type.can_be_indexed()) {
            return core::Status::InvalidArgument("Cannot create column: primary key column type must be indexable");
        }

        if(unique && !logical_type.can_be_indexed()) {
            return core::Status::InvalidArgument("Cannot create column: unique column type must be indexable");
        }

        return Column(
            std::move(name),
            logical_type,
            nullable,
            pk,
            unique
        );

    }

    Column::Column(
        std::string name,
        LogicalType logical_type,
        bool nullable,
        bool pk,
        bool unique
    ) :
        name_(std::move(name)),
        logical_type_(logical_type),
        nullable_(nullable),
        pk_(pk),
        unique_(unique)
    {}

    const std::string& Column::name() const {
        return name_;
    }

    LogicalType Column::logical_type() const {
        return logical_type_;
    }

    bool Column::nullable() const {
        return nullable_;
    }

    bool Column::pk() const {
        return pk_;
    }

    bool Column::unique() const {
        return unique_;
    }

    std::size_t Column::ordinal() const {
        return ordinal_;
    }

    std::size_t Column::fixed_offset() const {
        return fixed_offset_;
    }

    void Column::set_layout(std::size_t ordinal, std::size_t fixed_offset) {
        ordinal_ = ordinal;
        fixed_offset_ = fixed_offset;
    }

}
