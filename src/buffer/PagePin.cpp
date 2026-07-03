#include <dandb/buffer/PagePin.h>

#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/storage/Page.h>

namespace dandb::buffer {

    PagePin::PagePin(BufferPoolManager* bpm, storage::Page* page) :
        bpm_(bpm),
        page_(page)
    {}

    PagePin::~PagePin() {
        release();
    }

    PagePin::PagePin(PagePin&& other) noexcept :
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

    PagePin& PagePin::operator=(PagePin&& other) noexcept {

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

    storage::Page* PagePin::page() {
        return page_;
    }

    const storage::Page* PagePin::page() const {
        return page_;
    }

    void PagePin::mark_dirty() {
        is_dirty_ = true;
    }

    bool PagePin::is_dirty() const {
        return is_dirty_;
    }

    void PagePin::release() {
        if(owns_pin_) {
            static_cast<void>(bpm_->unpin_page(page_->id(), is_dirty_));
            owns_pin_ = false;
        }
    }

}
