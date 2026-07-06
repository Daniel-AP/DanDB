#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Row.h>

#include <vector>
#include <cstddef>
#include <span>

namespace dandb::record {

    class RowCodec {
        public:
            static core::Result<std::vector<std::byte>> encode(const Schema& schema, const Row& row);
            static core::Result<Row> decode(const Schema& schema, std::span<const std::byte> bytes);
    };

}