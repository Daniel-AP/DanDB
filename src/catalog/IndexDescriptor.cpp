#include <dandb/catalog/IndexDescriptor.h>

#include <dandb/core/Status.h>

#include <utility>

namespace dandb::catalog {

    core::Result<IndexDescriptor> IndexDescriptor::create(
        IndexId index_id,
        TableId table_id,
        std::string name,
        storage::PageId root_page_id,
        bool unique,
        bool primary,
        bool internal,
        ColumnId indexed_column_id
    ) {
        
        if(!index_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create index descriptor: index id cannot be invalid");
        }

        if(!table_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create index descriptor: table id cannot be invalid");
        }

        if(name.empty()) {
            return core::Status::InvalidArgument("Cannot create index descriptor: name cannot be empty");
        }

        if(!root_page_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create index descriptor: root page id cannot be invalid");
        }

        if(root_page_id == storage::HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot create index descriptor: root page id cannot be the database header page");
        }

        if(!indexed_column_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot create index descriptor: indexed column id cannot be invalid");
        }

        if(primary && !unique) {
            return core::Status::InvalidArgument("Cannot create index descriptor: primary index must be unique");
        }

        if(primary && !internal) {
            return core::Status::InvalidArgument("Cannot create index descriptor: primary index must be internal");
        }

        return IndexDescriptor(
            index_id,
            table_id,
            std::move(name),
            root_page_id,
            unique,
            primary,
            internal,
            indexed_column_id
        );

    }

    IndexDescriptor::IndexDescriptor(
        IndexId index_id,
        TableId table_id,
        std::string name,
        storage::PageId root_page_id,
        bool unique,
        bool primary,
        bool internal,
        ColumnId indexed_column_id
    ) :
        index_id_(index_id),
        table_id_(table_id),
        name_(std::move(name)),
        root_page_id_(root_page_id),
        unique_(unique),
        primary_(primary),
        internal_(internal),
        indexed_column_id_(indexed_column_id)
    {}

    IndexId IndexDescriptor::index_id() const {
        return index_id_;
    }

    TableId IndexDescriptor::table_id() const {
        return table_id_;
    }

    const std::string& IndexDescriptor::name() const {
        return name_;
    }

    storage::PageId IndexDescriptor::root_page_id() const {
        return root_page_id_;
    }

    bool IndexDescriptor::unique() const {
        return unique_;
    }

    bool IndexDescriptor::primary() const {
        return primary_;
    }

    bool IndexDescriptor::internal() const {
        return internal_;
    }

    ColumnId IndexDescriptor::indexed_column_id() const {
        return indexed_column_id_;
    }

}
