#include <dandb/record/RowCodec.h>

#include "RowValidation.h"

#include <dandb/core/Bytes.h>
#include <dandb/core/Endian.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace dandb::record {

    core::Result<std::vector<std::byte>> RowCodec::encode(const Schema& schema, const Row& row) {

        auto row_status = validate_row_against_schema(schema, row);
        if(!row_status.ok()) {
            return row_status;
        }

        std::vector<std::byte> bytes(schema.row_size());

        for(std::size_t i = 0; i < row.value_count(); i++) {

            if(!row.value(i).is_null()) continue;

            std::size_t null_bitmap_byte = i/8;
            std::size_t null_bitmap_bit = i%8;
            bytes[null_bitmap_byte] |= std::byte{ static_cast<std::uint8_t>(1U<<null_bitmap_bit) };

        }

        for(std::size_t i = 0; i < schema.column_count(); i++) {

            const auto& col = schema.column(i);
            const auto& val = row.value(i);

            if(val.is_null()) {
                continue;
            }

            const std::size_t offset = col.fixed_offset();

            switch(col.logical_type().kind()) {
                case LogicalType::Kind::Int8:
                    bytes[offset] = static_cast<std::byte>(
                        static_cast<std::uint8_t>(static_cast<std::int8_t>(val.as_integer()))
                    );
                    break;

                case LogicalType::Kind::Int16: {
                    auto status = core::write_u16_le(
                        bytes,
                        offset,
                        static_cast<std::uint16_t>(static_cast<std::int16_t>(val.as_integer()))
                    );
                    if(!status.ok()) {
                        return status;
                    }
                    break;
                }

                case LogicalType::Kind::Int32: {
                    auto status = core::write_u32_le(
                        bytes,
                        offset,
                        static_cast<std::uint32_t>(static_cast<std::int32_t>(val.as_integer()))
                    );
                    if(!status.ok()) {
                        return status;
                    }
                    break;
                }

                case LogicalType::Kind::Int64: {
                    auto status = core::write_u64_le(
                        bytes,
                        offset,
                        static_cast<std::uint64_t>(val.as_integer())
                    );
                    if(!status.ok()) {
                        return status;
                    }
                    break;
                }

                case LogicalType::Kind::Float64: {
                    auto status = core::write_u64_le(
                        bytes,
                        offset,
                        std::bit_cast<std::uint64_t>(val.as_float64())
                    );
                    if(!status.ok()) {
                        return status;
                    }
                    break;
                }

                case LogicalType::Kind::String: {
                    const auto& string_value = val.as_string();
                    std::memcpy(bytes.data()+offset, string_value.data(), string_value.size());
                    break;
                }

                case LogicalType::Kind::Boolean:
                    bytes[offset] = val.as_boolean() ? std::byte{ 0x01 } : std::byte{ 0x00 };
                    break;
            }

        }

        return bytes;

    }

    core::Result<Row> RowCodec::decode(const Schema& schema, std::span<const std::byte> bytes) {

        if(schema.row_size() != bytes.size()) {
            return core::Status::InvalidArgument("Cannot decode row: row byte count does not match schema row size");
        }

        std::vector<Value> values;
        values.reserve(schema.column_count());

        for(std::size_t i = 0; i < schema.column_count(); i++) {

            const auto& col = schema.column(i);

            const std::uint8_t null_bitmap_byte = std::to_integer<std::uint8_t>(bytes[i/8]);
            const bool is_null = (null_bitmap_byte&static_cast<std::uint8_t>(1ULL<<(i%8))) != 0;

            if(!is_null) continue;

            if(!col.nullable()) {
                return core::Status::InvalidArgument("Cannot decode row: row value is null but column is not nullable");
            }

            const std::size_t offset = col.fixed_offset();
            const auto payload = bytes.subspan(offset, col.logical_type().fixed_size());

            if(!core::bytes_are_zero(payload)) {
                return core::Status::InvalidArgument("Cannot decode row: null value payload bytes must be zero");
            }

        }

        const std::size_t used_bits_in_last_null_bitmap_byte = schema.column_count()%8;

        if(used_bits_in_last_null_bitmap_byte != 0) {
            const std::uint8_t unused_null_bitmap_bits_mask = static_cast<std::uint8_t>(0xFFU<<used_bits_in_last_null_bitmap_byte);

            if((bytes[schema.null_bitmap_size()-1]&std::byte{ unused_null_bitmap_bits_mask }) != std::byte{ 0 }) {
                return core::Status::InvalidArgument("Cannot decode row: unused null bitmap bits must be zero");
            }
        }

        for(std::size_t i = 0; i < schema.column_count(); i++) {

            const auto& col = schema.column(i);

            const std::uint8_t null_bitmap_byte = std::to_integer<std::uint8_t>(bytes[i/8]);
            const bool is_null = (null_bitmap_byte&static_cast<std::uint8_t>(1ULL<<(i%8))) != 0;

            if(is_null) {
                values.push_back(Value::null(col.logical_type()));
                continue;
            }

            const std::size_t offset = col.fixed_offset();

            switch(col.logical_type().kind()) {
                case LogicalType::Kind::Int8: {
                    const auto raw_value = std::to_integer<std::uint8_t>(bytes[offset]);
                    auto value = Value::int8(std::bit_cast<std::int8_t>(raw_value));
                    if(!value.ok()) {
                        return value.status();
                    }
                    values.push_back(value.value());
                    break;
                }

                case LogicalType::Kind::Int16: {
                    auto raw_value = core::read_u16_le(bytes, offset);
                    if(!raw_value.ok()) {
                        return raw_value.status();
                    }

                    auto value = Value::int16(std::bit_cast<std::int16_t>(raw_value.value()));
                    if(!value.ok()) {
                        return value.status();
                    }
                    values.push_back(value.value());
                    break;
                }

                case LogicalType::Kind::Int32: {
                    auto raw_value = core::read_u32_le(bytes, offset);
                    if(!raw_value.ok()) {
                        return raw_value.status();
                    }

                    auto value = Value::int32(std::bit_cast<std::int32_t>(raw_value.value()));
                    if(!value.ok()) {
                        return value.status();
                    }
                    values.push_back(value.value());
                    break;
                }

                case LogicalType::Kind::Int64: {
                    auto raw_value = core::read_u64_le(bytes, offset);
                    if(!raw_value.ok()) {
                        return raw_value.status();
                    }

                    values.push_back(Value::int64(std::bit_cast<std::int64_t>(raw_value.value())));
                    break;
                }

                case LogicalType::Kind::Float64: {
                    auto raw_value = core::read_u64_le(bytes, offset);
                    if(!raw_value.ok()) {
                        return raw_value.status();
                    }

                    values.push_back(Value::float64(std::bit_cast<double>(raw_value.value())));
                    break;
                }

                case LogicalType::Kind::String: {
                    std::string string_value;
                    const std::size_t capacity = *col.logical_type().capacity();
                    string_value.reserve(capacity);

                    std::size_t string_size = 0;
                    for(; string_size < capacity; string_size++) {
                        const char current = static_cast<char>(std::to_integer<std::uint8_t>(bytes[offset+string_size]));
                        if(current == '\0') {
                            break;
                        }
                        string_value += current;
                    }

                    if(string_size < capacity) {
                        const auto padding = bytes.subspan(offset+string_size, capacity-string_size);
                        if(!core::bytes_are_zero(padding)) {
                            return core::Status::InvalidArgument("Cannot decode row: string padding bytes must be zero");
                        }
                    }

                    auto value = Value::string(string_value, capacity);
                    if(!value.ok()) {
                        return value.status();
                    }
                    values.push_back(value.value());
                    break;
                }

                case LogicalType::Kind::Boolean: {
                    const auto value = std::to_integer<std::uint8_t>(bytes[offset]);
                    if(value > 1) {
                        return core::Status::InvalidArgument("Cannot decode row: boolean value must be 0x00 or 0x01");
                    }

                    values.push_back(Value::boolean(value == 1));
                    break;
                }
            }

        }

        return Row(values);

    }

}
