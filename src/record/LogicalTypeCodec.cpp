#include <dandb/record/LogicalTypeCodec.h>

#include <cstdint>

namespace dandb::record {

    std::uint8_t LogicalTypeCodec::encode_kind(LogicalType::Kind kind) {

        switch(kind) {
            case LogicalType::Kind::Int8:
                return 1;
            case LogicalType::Kind::Int16:
                return 2;
            case LogicalType::Kind::Int32:
                return 3;
            case LogicalType::Kind::Int64:
                return 4;
            case LogicalType::Kind::Float64:
                return 5;
            case LogicalType::Kind::String:
                return 6;
            case LogicalType::Kind::Boolean:
                return 7;
        }

        return 0;
    }

    core::Result<LogicalType::Kind> LogicalTypeCodec::decode_kind(std::uint8_t code) {

        switch(code) {
            case 1:
                return LogicalType::Kind::Int8;
            case 2:
                return LogicalType::Kind::Int16;
            case 3:
                return LogicalType::Kind::Int32;
            case 4:
                return LogicalType::Kind::Int64;
            case 5:
                return LogicalType::Kind::Float64;
            case 6:
                return LogicalType::Kind::String;
            case 7:
                return LogicalType::Kind::Boolean;
            default:
                return core::Status::InvalidArgument("Cannot decode logical type kind: unknown code");
        }
    }

}
