#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Value.h>

#include <cstddef>
#include <vector>

namespace dandb::record {

    class KeyCodec {
        public:
            static core::Result<std::vector<std::byte>> encode(const Value& value);
    };

}
