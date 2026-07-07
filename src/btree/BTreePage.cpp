#include <dandb/btree/BTreePage.h>

#include <dandb/core/Endian.h>

namespace dandb::btree {

    core::Status initialize_internal(std::span<std::byte> bytes, std::uint16_t key_size, std::uint16_t value_size) {

        if(bytes.size() != core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot initialize internal B+ tree page: page size is invalid");
        }

        if(key_size == 0) {
            return core::Status::InvalidArgument("Cannot initialize internal B+ tree page: key size must be greater than 0");
        }

        if(value_size == 0) {
            return core::Status::InvalidArgument("Cannot initialize internal B+ tree page: value size must be greater than 0");
        }

        const std::size_t entry_size = key_size + sizeof(std::uint64_t);
        const std::size_t capacity = BTREE_PAGE_ENTRY_AREA_SIZE / entry_size;
        if(capacity == 0) {
            return core::Status::InvalidArgument("Cannot initialize internal B+ tree page: entry size is too large");
        }

        for(std::size_t i = 0; i < bytes.size(); i++) {
            bytes[i] = std::byte{ 0 };
        }

        bytes[BTREE_PAGE_KIND_OFFSET] = static_cast<std::byte>(BTREE_INTERNAL_PAGE_KIND);
        bytes[BTREE_PAGE_FLAGS_OFFSET] = std::byte{ 0 };

        auto status = core::write_u16_le(bytes, BTREE_PAGE_KEY_COUNT_OFFSET, 0);
        if(!status.ok()) {
            return status;
        }

        status = core::write_u16_le(bytes, BTREE_PAGE_HEADER_SIZE_OFFSET, static_cast<std::uint16_t>(BTREE_PAGE_HEADER_SIZE));
        if(!status.ok()) {
            return status;
        }

        status = core::write_u64_le(bytes, BTREE_PAGE_PARENT_PAGE_ID_OFFSET, storage::INVALID_PAGE_ID.id);
        if(!status.ok()) {
            return status;
        }

        status = core::write_u64_le(bytes, BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET, storage::INVALID_PAGE_ID.id);
        if(!status.ok()) {
            return status;
        }

        status = core::write_u64_le(bytes, BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET, storage::INVALID_PAGE_ID.id);
        if(!status.ok()) {
            return status;
        }

        status = core::write_u64_le(bytes, BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET, storage::INVALID_PAGE_ID.id);
        if(!status.ok()) {
            return status;
        }

        status = core::write_u16_le(bytes, BTREE_PAGE_KEY_SIZE_OFFSET, key_size);
        if(!status.ok()) {
            return status;
        }

        return core::write_u16_le(bytes, BTREE_PAGE_VALUE_SIZE_OFFSET, value_size);

    }

}
