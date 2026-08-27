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
        
        if(name.empty()) return core::Status::InvalidArgument("Column name cannot be empty");

        if(pk && nullable) {
            return core::Status::InvalidArgument("Column '"+name+"' cannot be both nullable and PRIMARY KEY");
        }

        if(unique && nullable) {
            return core::Status::InvalidArgument("Column '"+name+"' cannot be both nullable and UNIQUE");
        }

        if(pk && !logical_type.can_be_indexed()) {
            const std::string message = "Column '"+name+"' cannot use "+logical_type.display_name()+" as PRIMARY KEY";
            return core::Status::InvalidArgument(message);
        }

        if(unique && !logical_type.can_be_indexed()) {
            const std::string message = "Column '"+name+"' cannot use "+logical_type.display_name()+" as UNIQUE";
            return core::Status::InvalidArgument(message);
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
