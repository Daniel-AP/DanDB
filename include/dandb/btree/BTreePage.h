#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/storage/PageId.h>

#include <concepts>
#include <type_traits>
#include <array>
#include <utility>
#include <span>
#include <cstddef>
#include <cstdint>

namespace dandb::btree {

    inline constexpr std::size_t BTREE_PAGE_KIND_OFFSET = 0;
    inline constexpr std::size_t BTREE_PAGE_FLAGS_OFFSET = 1;
    inline constexpr std::size_t BTREE_PAGE_KEY_COUNT_OFFSET = 2;
    inline constexpr std::size_t BTREE_PAGE_HEADER_SIZE_OFFSET = 4;
    inline constexpr std::size_t BTREE_PAGE_PARENT_PAGE_ID_OFFSET = 8;
    inline constexpr std::size_t BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET = 16;
    inline constexpr std::size_t BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET = 24;
    inline constexpr std::size_t BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET = 32;
    inline constexpr std::size_t BTREE_PAGE_KEY_SIZE_OFFSET = 40;
    inline constexpr std::size_t BTREE_PAGE_VALUE_SIZE_OFFSET = 42;

    inline constexpr std::size_t BTREE_PAGE_HEADER_SIZE = 64;
    inline constexpr std::size_t BTREE_PAGE_ENTRY_ARRAY_OFFSET = BTREE_PAGE_HEADER_SIZE;
    inline constexpr std::size_t BTREE_PAGE_ENTRY_AREA_SIZE = core::PAGE_SIZE - BTREE_PAGE_HEADER_SIZE;
    inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> BTREE_PAGE_RESERVED_RANGES{{
        { 6, 2 },
        { 44, 20 }
    }};

    inline constexpr std::uint8_t BTREE_INTERNAL_PAGE_KIND = 1;
    inline constexpr std::uint8_t BTREE_LEAF_PAGE_KIND = 2;
    inline constexpr std::uint8_t BTREE_PAGE_ROOT_FLAG = 0x01;

    template<class Byte>
    concept BTreePageByte = std::same_as<std::remove_const_t<Byte>, std::byte>;

    enum class BTreePageKind {
        Internal,
        Leaf
    };

    core::Status initialize_internal(std::span<std::byte> bytes, std::uint16_t key_size, std::uint16_t value_size);
    core::Status initialize_leaf(std::span<std::byte> bytes, std::uint16_t key_size, std::uint16_t value_size);
    core::Status validate(std::span<const std::byte> bytes);

    template<BTreePageByte Byte>
    class BTreePage {
        public:

            static core::Result<BTreePage<Byte>> open(std::span<Byte> bytes);

            BTreePageKind kind() const;
            bool is_root() const;
            std::uint16_t key_count() const;
            storage::PageId parent_page_id() const;
            storage::PageId next_leaf_page_id() const;
            storage::PageId previous_leaf_page_id() const;
            storage::PageId first_child_page_id() const;
            std::uint16_t key_size() const;
            std::uint16_t value_size() const;

            std::size_t leaf_entry_size() const;
            std::size_t internal_entry_size() const;
            std::size_t leaf_capacity() const;
            std::size_t internal_capacity() const;

            core::Status set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>);
            void set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>);
            void set_next_leaf_page_id(storage::PageId next_leaf_page_id) requires (!std::is_const_v<Byte>);
            void set_previous_leaf_page_id(storage::PageId previous_leaf_page_id) requires (!std::is_const_v<Byte>);
            void set_first_child_page_id(storage::PageId first_child_page_id) requires (!std::is_const_v<Byte>);
            void set_root(bool is_root) requires (!std::is_const_v<Byte>);

        private:
            explicit BTreePage(std::span<Byte> bytes);

            std::span<Byte> bytes_;
    };

    template<BTreePageByte Byte>
    core::Result<BTreePage<Byte>> BTreePage<Byte>::open(std::span<Byte> bytes) {

        auto status = validate(bytes);
        if(!status.ok()) {
            return status;
        }

        return BTreePage<Byte>{ bytes };

    }

    template<BTreePageByte Byte>
    BTreePage<Byte>::BTreePage(std::span<Byte> bytes) :
        bytes_(bytes)
    {}

    template<BTreePageByte Byte>
    BTreePageKind BTreePage<Byte>::kind() const {

        const auto stored_kind = std::to_integer<std::uint8_t>(bytes_[BTREE_PAGE_KIND_OFFSET]);
        if(stored_kind == BTREE_INTERNAL_PAGE_KIND) {
            return BTreePageKind::Internal;
        }

        return BTreePageKind::Leaf;

    }

    template<BTreePageByte Byte>
    bool BTreePage<Byte>::is_root() const {

        const auto flags = std::to_integer<std::uint8_t>(bytes_[BTREE_PAGE_FLAGS_OFFSET]);
        return (flags&BTREE_PAGE_ROOT_FLAG) != 0;

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreePage<Byte>::key_count() const {

        return core::read_u16_le(bytes_, BTREE_PAGE_KEY_COUNT_OFFSET).value();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreePage<Byte>::parent_page_id() const {

        return storage::PageId{ core::read_u64_le(bytes_, BTREE_PAGE_PARENT_PAGE_ID_OFFSET).value() };

    }

    template<BTreePageByte Byte>
    storage::PageId BTreePage<Byte>::next_leaf_page_id() const {

        return storage::PageId{ core::read_u64_le(bytes_, BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET).value() };

    }

    template<BTreePageByte Byte>
    storage::PageId BTreePage<Byte>::previous_leaf_page_id() const {

        return storage::PageId{ core::read_u64_le(bytes_, BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET).value() };

    }

    template<BTreePageByte Byte>
    storage::PageId BTreePage<Byte>::first_child_page_id() const {

        return storage::PageId{ core::read_u64_le(bytes_, BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET).value() };

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreePage<Byte>::key_size() const {

        return core::read_u16_le(bytes_, BTREE_PAGE_KEY_SIZE_OFFSET).value();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreePage<Byte>::value_size() const {

        return core::read_u16_le(bytes_, BTREE_PAGE_VALUE_SIZE_OFFSET).value();

    }

    template<BTreePageByte Byte>
    std::size_t BTreePage<Byte>::leaf_entry_size() const {

        return static_cast<std::size_t>(key_size())+value_size();

    }

    template<BTreePageByte Byte>
    std::size_t BTreePage<Byte>::internal_entry_size() const {

        return static_cast<std::size_t>(key_size())+sizeof(std::uint64_t);

    }

    template<BTreePageByte Byte>
    std::size_t BTreePage<Byte>::leaf_capacity() const {

        return BTREE_PAGE_ENTRY_AREA_SIZE/leaf_entry_size();

    }

    template<BTreePageByte Byte>
    std::size_t BTreePage<Byte>::internal_capacity() const {

        return BTREE_PAGE_ENTRY_AREA_SIZE/internal_entry_size();

    }

    template<BTreePageByte Byte>
    core::Status BTreePage<Byte>::set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>) {

        const std::size_t capacity = kind() == BTreePageKind::Leaf ? leaf_capacity() : internal_capacity();
        if(key_count > capacity) {
            return core::Status::InvalidArgument("Cannot set B+ tree page key count: key count exceeds page capacity");
        }

        return core::write_u16_le(bytes_, BTREE_PAGE_KEY_COUNT_OFFSET, key_count);

    }

    template<BTreePageByte Byte>
    void BTreePage<Byte>::set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>) {

        core::write_u64_le(bytes_, BTREE_PAGE_PARENT_PAGE_ID_OFFSET, parent_page_id.id);

    }

    template<BTreePageByte Byte>
    void BTreePage<Byte>::set_next_leaf_page_id(storage::PageId next_leaf_page_id) requires (!std::is_const_v<Byte>) {

        core::write_u64_le(bytes_, BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET, next_leaf_page_id.id);

    }

    template<BTreePageByte Byte>
    void BTreePage<Byte>::set_previous_leaf_page_id(storage::PageId previous_leaf_page_id) requires (!std::is_const_v<Byte>) {

        core::write_u64_le(bytes_, BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET, previous_leaf_page_id.id);

    }

    template<BTreePageByte Byte>
    void BTreePage<Byte>::set_first_child_page_id(storage::PageId first_child_page_id) requires (!std::is_const_v<Byte>) {

        core::write_u64_le(bytes_, BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET, first_child_page_id.id);

    }

    template<BTreePageByte Byte>
    void BTreePage<Byte>::set_root(bool is_root) requires (!std::is_const_v<Byte>) {

        auto flags = std::to_integer<std::uint8_t>(bytes_[BTREE_PAGE_FLAGS_OFFSET]);
        if(is_root) {
            flags = static_cast<std::uint8_t>(flags|BTREE_PAGE_ROOT_FLAG);
        } else {
            flags = static_cast<std::uint8_t>(flags&~BTREE_PAGE_ROOT_FLAG);
        }

        bytes_[BTREE_PAGE_FLAGS_OFFSET] = static_cast<std::byte>(flags);

    }

}
