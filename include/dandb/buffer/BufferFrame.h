#pragma once

#include <dandb/storage/Page.h>
#include <dandb/core/Status.h>

#include <cstdint>

namespace dandb::buffer {

    class BufferFrame {
        public:
            BufferFrame() : page_{} {}
            explicit BufferFrame(const storage::Page& page) : page_(page) {}

            const storage::Page& page() const {
                return page_;
            }

            storage::Page& page() {
                return page_;
            }

            void pin() {
                pin_count_++;
            }

            [[nodiscard]] core::Status unpin() {
                if(pin_count_ > 0) {
                    pin_count_--;
                    return core::Status::Ok();
                }
                return core::Status::InternalError("Cannot unpin buffer frame with pin count 0");
            }

            std::uint32_t pin_count() const {
                return pin_count_;
            }

            bool is_pinned() const {
                return pin_count_ > 0;
            }

            void mark_dirty() {
                is_dirty_ = true;
            }

            void clear_dirty() {
                is_dirty_ = false;
            }

            bool is_dirty() const {
                return is_dirty_;
            }

            void reset() {
                page_ = storage::Page();
                pin_count_ = 0;
                is_dirty_ = false;
            }
        
        private:
            storage::Page page_;
            std::uint32_t pin_count_ = 0;
            bool is_dirty_ = false;
    };

}