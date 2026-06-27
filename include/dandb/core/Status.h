#pragma once

#include <string>
#include <utility>

namespace dandb::core {

    enum class StatusCode {
        Ok,
        InvalidArgument,
        IoError,
        Corruption,
        NotFound,
        AlreadyExists,
        ConstraintViolation,
        TransactionError,
        ParseError,
        InternalError
    };

    class Status {
        public:
            bool ok() const;
            StatusCode code() const;
            const std::string& message() const;

            static Status Ok();
            static Status InvalidArgument(std::string message);
            static Status IoError(std::string message);
            static Status Corruption(std::string message);
            static Status NotFound(std::string message);
            static Status AlreadyExists(std::string message);
            static Status ConstraintViolation(std::string message);
            static Status TransactionError(std::string message);
            static Status ParseError(std::string message);
            static Status InternalError(std::string message);

        private:
            StatusCode code_;
            std::string message_;
            Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}
    };

}