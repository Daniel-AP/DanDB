#include <dandb/catalog/TableDescriptor.h>

#include <dandb/core/Status.h>

#include <utility>

namespace dandb::catalog {

    core::Result<TableDescriptor> TableDescriptor::create(
        TableId table_id,
        std::string name,
        storage::PageId root_page_id,
        ColumnId primary_key_column_id
    ) {
        
        if(!table_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create table descriptor: table id cannot be invalid");
        }

        if(name.empty()) {
            return core::Status::InvalidArgument("Cannot create table descriptor: name cannot be empty");
        }

        if(!root_page_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create table descriptor: root page id cannot be invalid");
        }

        if(root_page_id == storage::HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot create table descriptor: root page id cannot be the database header page");
        }

        if(!primary_key_column_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create table descriptor: primary key column id cannot be invalid");
        }

        return TableDescriptor(
            table_id,
            std::move(name),
            root_page_id,
            primary_key_column_id
        );

    }

    TableDescriptor::TableDescriptor(
        TableId table_id,
        std::string name,
        storage::PageId root_page_id,
        ColumnId primary_key_column_id
    ) :
        table_id_(table_id),
        name_(std::move(name)),
        root_page_id_(root_page_id),
        primary_key_column_id_(primary_key_column_id)
    {}

    TableId TableDescriptor::table_id() const {
        return table_id_;
    }

    const std::string& TableDescriptor::name() const {
        return name_;
    }

    storage::PageId TableDescriptor::root_page_id() const {
        return root_page_id_;
    }

    ColumnId TableDescriptor::primary_key_column_id() const {
        return primary_key_column_id_;
    }

}
