#include <dandb/core/Status.h>

#include <string>
#include <utility>

namespace dandb::core {

    bool Status::ok() const {
        return code_ == StatusCode::Ok;
    }

    StatusCode Status::code() const {
        return code_;
    }

    const std::string& Status::message() const {
        return message_;
    }

    Status Status::Ok() {
        return Status(StatusCode::Ok, {});
    }

    Status Status::InvalidArgument(std::string message) {
        return Status(StatusCode::InvalidArgument, std::move(message));
    }

    Status Status::IoError(std::string message) {
        return Status(StatusCode::IoError, std::move(message));
    }

    Status Status::Corruption(std::string message) {
        return Status(StatusCode::Corruption, std::move(message));
    }

    Status Status::NotFound(std::string message) {
        return Status(StatusCode::NotFound, std::move(message));
    }

    Status Status::AlreadyExists(std::string message) {
        return Status(StatusCode::AlreadyExists, std::move(message));
    }

    Status Status::ConstraintViolation(std::string message) {
        return Status(StatusCode::ConstraintViolation, std::move(message));
    }

    Status Status::TransactionError(std::string message) {
        return Status(StatusCode::TransactionError, std::move(message));
    }

    Status Status::ParseError(std::string message) {
        return Status(StatusCode::ParseError, std::move(message));
    }

    Status Status::InternalError(std::string message) {
        return Status(StatusCode::InternalError, std::move(message));
    }

}