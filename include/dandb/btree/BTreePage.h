#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/core/Constants.h>
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

}
