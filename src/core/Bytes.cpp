#include <dandb/core/Bytes.h>

#include <span>
#include <cstddef>

namespace dandb::core {

    bool bytes_are_zero(std::span<std::byte> bytes) {

        for(const std::byte& byte: bytes) {
            if(byte != std::byte{ 0 }) return false;
        }

        return true;

    }

}