#include <dandb/buffer/PageGuard.h>

#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/storage/Page.h>

namespace dandb::buffer {

    PageGuard::PageGuard(BufferPoolManager* bpm, storage::Page* page) :
        bpm_(bpm),
        page_(page)
    {}

    PageGuard::~PageGuard() {
        release();
    }

    PageGuard::PageGuard(PageGuard&& other) noexcept :
        bpm_(other.bpm_),
        page_(other.page_),
        is_dirty_(other.is_dirty_),
        owns_pin_(other.owns_pin_)
    {
        other.bpm_ = nullptr;
        other.page_ = nullptr;
        other.is_dirty_ = false;
        other.owns_pin_ = false;
    }

    PageGuard& PageGuard::operator=(PageGuard&& other) noexcept {

        if(this != &other) {
            release();

            bpm_ = other.bpm_;
            page_ = other.page_;
            is_dirty_ = other.is_dirty_;
            owns_pin_ = other.owns_pin_;

            other.bpm_ = nullptr;
            other.page_ = nullptr;
            other.is_dirty_ = false;
            other.owns_pin_ = false;
        }

        return *this;

    }

    storage::Page* PageGuard::page() {
        return page_;
    }

    const storage::Page* PageGuard::page() const {
        return page_;
    }

    void PageGuard::mark_dirty() {
        is_dirty_ = true;
    }

    bool PageGuard::is_dirty() const {
        return is_dirty_;
    }

    void PageGuard::release() {
        if(owns_pin_) {
            static_cast<void>(bpm_->unpin_page(page_->id(), is_dirty_));
            owns_pin_ = false;
        }
    }

}
