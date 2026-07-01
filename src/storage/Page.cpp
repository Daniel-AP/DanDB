#include <dandb/storage/Page.h>

namespace dandb::storage {

    Page::Page(PageId id) : id_(id), data_{} {}

    PageId Page::id() const {
        return id_;
    }

    void Page::set_id(PageId new_id) {
        id_ = new_id;
    }

    std::array<std::byte, core::PAGE_SIZE>& Page::data() {
        return data_;
    }

    const std::array<std::byte, core::PAGE_SIZE>& Page::data() const {
        return data_;
    }

}