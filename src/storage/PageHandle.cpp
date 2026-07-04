#include <dandb/storage/PageHandle.h>

#include <dandb/storage/Pager.h>

#include <utility>

namespace dandb::storage {

    PageHandle::PageHandle(Pager* pager, buffer::PagePin page_pin) :
        pager_(pager),
        page_pin_(std::move(page_pin))
    {}

    const Page* PageHandle::page() const {
        return page_pin_.page();
    }

    core::Result<Page*> PageHandle::mutable_page() {

        auto status = mark_dirty();
        if(!status.ok()) {
            return status;
        }

        return page_pin_.page();

    }

    core::Status PageHandle::mark_dirty() {

        Page* current_page = page_pin_.page();
        if(current_page == nullptr) {
            return core::Status::InternalError("Cannot mark page dirty: page handle does not reference a page");
        }

        auto status = pager_->mark_dirty(current_page->id());
        if(!status.ok()) {
            return status;
        }

        page_pin_.mark_dirty();
        return core::Status::Ok();

    }

    bool PageHandle::is_dirty() const {
        return page_pin_.is_dirty();
    }

}
