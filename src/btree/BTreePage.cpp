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

        const std::size_t entry_size = key_size+sizeof(std::uint64_t);
        const std::size_t capacity = BTREE_PAGE_ENTRY_AREA_SIZE/entry_size;
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

    core::Status initialize_leaf(std::span<std::byte> bytes, std::uint16_t key_size, std::uint16_t value_size) {

        if(bytes.size() != core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot initialize leaf B+ tree page: page size is invalid");
        }

        if(key_size == 0) {
            return core::Status::InvalidArgument("Cannot initialize leaf B+ tree page: key size must be greater than 0");
        }

        if(value_size == 0) {
            return core::Status::InvalidArgument("Cannot initialize leaf B+ tree page: value size must be greater than 0");
        }

        const std::size_t entry_size = key_size+value_size;
        const std::size_t capacity = BTREE_PAGE_ENTRY_AREA_SIZE/entry_size;
        if(capacity == 0) {
            return core::Status::InvalidArgument("Cannot initialize leaf B+ tree page: entry size is too large");
        }

        for(std::size_t i = 0; i < bytes.size(); i++) {
            bytes[i] = std::byte{ 0 };
        }

        bytes[BTREE_PAGE_KIND_OFFSET] = static_cast<std::byte>(BTREE_LEAF_PAGE_KIND);
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

    core::Status validate(std::span<const std::byte> bytes) {

        if(bytes.size() != core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot validate B+ tree page: page size is invalid");
        }

        const auto page_kind = std::to_integer<std::uint8_t>(bytes[BTREE_PAGE_KIND_OFFSET]);
        if(page_kind != BTREE_INTERNAL_PAGE_KIND && page_kind != BTREE_LEAF_PAGE_KIND) {
            return core::Status::Corruption("Cannot validate B+ tree page: page kind is invalid");
        }

        const auto flags = std::to_integer<std::uint8_t>(bytes[BTREE_PAGE_FLAGS_OFFSET]);
        if((flags&~BTREE_PAGE_ROOT_FLAG) != 0) {
            return core::Status::Corruption("Cannot validate B+ tree page: flags contain unsupported bits");
        }

        auto stored_key_count_result = core::read_u16_le(bytes, BTREE_PAGE_KEY_COUNT_OFFSET);
        if(!stored_key_count_result.ok()) {
            return stored_key_count_result.status();
        }

        const auto stored_key_count = stored_key_count_result.value();

        auto stored_header_size_result = core::read_u16_le(bytes, BTREE_PAGE_HEADER_SIZE_OFFSET);
        if(!stored_header_size_result.ok()) {
            return stored_header_size_result.status();
        }

        const auto stored_header_size = static_cast<std::size_t>(stored_header_size_result.value());
        if(stored_header_size != BTREE_PAGE_HEADER_SIZE) {
            return core::Status::Corruption("Cannot validate B+ tree page: header size is invalid");
        }

        auto stored_key_size_result = core::read_u16_le(bytes, BTREE_PAGE_KEY_SIZE_OFFSET);
        if(!stored_key_size_result.ok()) {
            return stored_key_size_result.status();
        }

        const auto stored_key_size = stored_key_size_result.value();
        if(stored_key_size == 0) {
            return core::Status::Corruption("Cannot validate B+ tree page: key size must be greater than 0");
        }

        auto stored_value_size_result = core::read_u16_le(bytes, BTREE_PAGE_VALUE_SIZE_OFFSET);
        if(!stored_value_size_result.ok()) {
            return stored_value_size_result.status();
        }

        const auto stored_value_size = stored_value_size_result.value();
        if(stored_value_size == 0) {
            return core::Status::Corruption("Cannot validate B+ tree page: value size must be greater than 0");
        }

        const std::size_t entry_size = page_kind == BTREE_LEAF_PAGE_KIND
            ? static_cast<std::size_t>(stored_key_size)+stored_value_size
            : static_cast<std::size_t>(stored_key_size)+sizeof(std::uint64_t);
        const std::size_t capacity = BTREE_PAGE_ENTRY_AREA_SIZE/entry_size;

        if(capacity == 0) {
            return core::Status::Corruption("Cannot validate B+ tree page: entry size is too large");
        }

        if(stored_key_count > capacity) {
            return core::Status::Corruption("Cannot validate B+ tree page: key count exceeds page capacity");
        }

        return core::Status::Ok();

    }

}
