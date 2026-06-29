#pragma once

#include <dandb/core/Status.h>

#include <cstddef>
#include <cstdint>

namespace dandb::platform {

    class FileFaultInjector {
        public:
            virtual ~FileFaultInjector() = default;

            virtual dandb::core::Status before_write(std::uint64_t offset, std::size_t byte_count) {
                return dandb::core::Status::Ok();
            }

            virtual dandb::core::Status before_sync() {
                return dandb::core::Status::Ok();
            }

            virtual dandb::core::Status after_sync() {
                return dandb::core::Status::Ok();
            }

            virtual dandb::core::Status before_resize(std::uint64_t new_size) {
                return dandb::core::Status::Ok();
            }

            virtual dandb::core::Status after_resize_file_pointer(std::uint64_t new_size) {
                return dandb::core::Status::Ok();
            }

            virtual dandb::core::Status after_resize_end_of_file(std::uint64_t new_size) {
                return dandb::core::Status::Ok();
            }


    };

}