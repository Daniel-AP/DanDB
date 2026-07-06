#include <dandb/record/KeyCodec.h>

#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace dandb::record {

    core::Result<std::vector<std::byte>> KeyCodec::encode(const Value& value) {

        if(value.is_null()) {
            return core::Status::InvalidArgument("Cannot encode key: null keys are not supported");
        }

        switch(value.type().kind()) {
            case LogicalType::Kind::Int8: {
                std::vector<std::byte> bytes(1);
                const auto integer_value = static_cast<std::int8_t>(value.as_integer());
                const auto unsigned_value = static_cast<std::uint8_t>(integer_value);
                constexpr std::uint8_t sign_bit = 0x80u;
                const auto sortable_value = static_cast<std::uint8_t>(unsigned_value^sign_bit);
                bytes[0] = static_cast<std::byte>(sortable_value);
                return bytes;
            }

            case LogicalType::Kind::Int16: {
                std::vector<std::byte> bytes(sizeof(std::uint16_t));
                const auto integer_value = static_cast<std::int16_t>(value.as_integer());
                const auto unsigned_value = static_cast<std::uint16_t>(integer_value);
                constexpr std::uint16_t sign_bit = 0x8000u;
                const auto sortable_value = static_cast<std::uint16_t>(unsigned_value^sign_bit);
                auto status = core::write_u16_be(bytes, 0, sortable_value);
                if(!status.ok()) {
                    return status;
                }
                return bytes;
            }

            case LogicalType::Kind::Int32: {
                std::vector<std::byte> bytes(sizeof(std::uint32_t));
                const auto integer_value = static_cast<std::int32_t>(value.as_integer());
                const auto unsigned_value = static_cast<std::uint32_t>(integer_value);
                constexpr std::uint32_t sign_bit = 0x80000000u;
                const auto sortable_value = unsigned_value^sign_bit;
                auto status = core::write_u32_be(bytes, 0, sortable_value);
                if(!status.ok()) {
                    return status;
                }
                return bytes;
            }

            case LogicalType::Kind::Int64: {
                std::vector<std::byte> bytes(sizeof(std::uint64_t));
                const auto integer_value = value.as_integer();
                const auto unsigned_value = static_cast<std::uint64_t>(integer_value);
                constexpr std::uint64_t sign_bit = 0x8000000000000000ULL;
                const auto sortable_value = unsigned_value^sign_bit;
                auto status = core::write_u64_be(bytes, 0, sortable_value);
                if(!status.ok()) {
                    return status;
                }
                return bytes;
            }

            case LogicalType::Kind::String: {
                const auto& string_value = value.as_string();
                if(string_value.find('\0') != std::string::npos) {
                    return core::Status::InvalidArgument("Cannot encode key: string value cannot contain a null character");
                }

                std::vector<std::byte> bytes(*value.type().capacity());
                std::memcpy(bytes.data(), string_value.data(), string_value.size());
                return bytes;
            }

            case LogicalType::Kind::Boolean:
                return std::vector<std::byte>{ value.as_boolean() ? std::byte{ 0x01 } : std::byte{ 0x00 } };

            case LogicalType::Kind::Float64:
                return core::Status::InvalidArgument("Cannot encode key: DOUBLE keys are not supported");
        }

        return core::Status::InternalError("Cannot encode key: unknown logical type");

    }

}
