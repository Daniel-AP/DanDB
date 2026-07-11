#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/LogicalType.h>

#include <cstdint>

namespace dandb::record {

    class LogicalTypeCodec {
        public:
            static std::uint8_t encode_kind(LogicalType::Kind kind);
            static core::Result<LogicalType::Kind> decode_kind(std::uint8_t code);
    };

}
