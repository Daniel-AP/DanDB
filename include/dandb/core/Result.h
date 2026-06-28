#pragma once

#include <dandb/core/Status.h>

#include <optional>
#include <utility>
#include <stdexcept>

namespace dandb::core {

    template<typename T>
    class Result {
        public:
            Result(Status status) : status_(std::move(status)) {
                if(status.ok()) throw std::invalid_argument("Status must not be Ok");
            }

            Result(T value) : value_(std::move(value)), status_(Status::Ok()) {}

            bool ok() const {
                return value_.has_value();
            }

            const Status& status() const {
                return status_;
            }

            T& value() {
                return *value_;
            }

            const T& value() const {
                return *value_;
            }

        private:
            Status status_;
            std::optional<T> value_;
    };

}