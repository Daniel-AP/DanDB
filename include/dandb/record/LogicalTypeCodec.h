#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/LogicalType.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dandb::record {

    class LogicalTypeCodec {
        public:
            static std::uint8_t encode_kind(LogicalType::Kind kind);
            static core::Result<LogicalType::Kind> decode_kind(std::uint8_t code);
            static core::Result<LogicalType> decode(std::uint8_t kind_code,std::optional<std::size_t> capacity);
    };

}
