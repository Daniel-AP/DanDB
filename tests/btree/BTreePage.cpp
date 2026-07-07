#include <catch_amalgamated.hpp>

#include <dandb/btree/BTreePage.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Endian.h>
#include <dandb/core/Status.h>
#include <dandb/storage/PageId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using dandb::btree::BTreePage;
using dandb::btree::BTreePageKind;
using dandb::btree::BTREE_PAGE_ENTRY_AREA_SIZE;
using dandb::btree::BTREE_PAGE_ENTRY_ARRAY_OFFSET;
using dandb::btree::BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET;
using dandb::btree::BTREE_PAGE_FLAGS_OFFSET;
using dandb::btree::BTREE_PAGE_HEADER_SIZE;
using dandb::btree::BTREE_PAGE_HEADER_SIZE_OFFSET;
using dandb::btree::BTREE_PAGE_KEY_COUNT_OFFSET;
using dandb::btree::BTREE_PAGE_KEY_SIZE_OFFSET;
using dandb::btree::BTREE_PAGE_KIND_OFFSET;
using dandb::btree::BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET;
using dandb::btree::BTREE_PAGE_PARENT_PAGE_ID_OFFSET;
using dandb::btree::BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET;
using dandb::btree::BTREE_PAGE_ROOT_FLAG;
using dandb::btree::BTREE_PAGE_VALUE_SIZE_OFFSET;
using dandb::btree::BTREE_INTERNAL_PAGE_KIND;
using dandb::btree::BTREE_LEAF_PAGE_KIND;
using dandb::btree::initialize_internal;
using dandb::btree::initialize_leaf;
using dandb::btree::validate;
using dandb::core::PAGE_SIZE;
using dandb::core::StatusCode;
using dandb::core::read_u16_le;
using dandb::core::read_u64_le;
using dandb::core::write_u16_le;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::PageId;

namespace {

    using PageBytes = std::array<std::byte, PAGE_SIZE>;

    PageBytes make_leaf_page(std::uint16_t key_size = 8, std::uint16_t value_size = 32) {
        PageBytes bytes{};

        REQUIRE(initialize_leaf(bytes, key_size, value_size).ok());
        return bytes;
    }

    PageBytes make_internal_page(std::uint16_t key_size = 8, std::uint16_t value_size = 32) {
        PageBytes bytes{};

        REQUIRE(initialize_internal(bytes, key_size, value_size).ok());
        return bytes;
    }

    void require_corruption(const PageBytes& bytes) {
        const auto status = validate(bytes);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::Corruption);
    }

    template<class Page>
    concept SupportsSetRoot = requires(Page page) {
        page.set_root(true);
    };

}

TEST_CASE("B+ tree page exposes the documented header layout constants", "[btree][page]") {
    static_assert(BTREE_PAGE_KIND_OFFSET == 0);
    static_assert(BTREE_PAGE_FLAGS_OFFSET == 1);
    static_assert(BTREE_PAGE_KEY_COUNT_OFFSET == 2);
    static_assert(BTREE_PAGE_HEADER_SIZE_OFFSET == 4);
    static_assert(BTREE_PAGE_PARENT_PAGE_ID_OFFSET == 8);
    static_assert(BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET == 16);
    static_assert(BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET == 24);
    static_assert(BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET == 32);
    static_assert(BTREE_PAGE_KEY_SIZE_OFFSET == 40);
    static_assert(BTREE_PAGE_VALUE_SIZE_OFFSET == 42);
    static_assert(BTREE_PAGE_HEADER_SIZE == 64);
    static_assert(BTREE_PAGE_ENTRY_ARRAY_OFFSET == BTREE_PAGE_HEADER_SIZE);
    static_assert(BTREE_PAGE_ENTRY_AREA_SIZE == PAGE_SIZE - BTREE_PAGE_HEADER_SIZE);
}

TEST_CASE("B+ tree page constness controls whether setters are available", "[btree][page]") {
    static_assert(SupportsSetRoot<BTreePage<std::byte>>);
    static_assert(!SupportsSetRoot<BTreePage<const std::byte>>);
}

TEST_CASE("B+ tree leaf initialization writes the documented header bytes", "[btree][page]") {
    PageBytes bytes{};
    bytes.fill(std::byte{ 0xAA });

    const auto status = initialize_leaf(bytes, 8, 32);

    REQUIRE(status.ok());
    REQUIRE(bytes[BTREE_PAGE_KIND_OFFSET] == static_cast<std::byte>(BTREE_LEAF_PAGE_KIND));
    REQUIRE(bytes[BTREE_PAGE_FLAGS_OFFSET] == std::byte{ 0 });

    const auto key_count = read_u16_le(bytes, BTREE_PAGE_KEY_COUNT_OFFSET);
    REQUIRE(key_count.ok());
    REQUIRE(key_count.value() == 0);

    const auto header_size = read_u16_le(bytes, BTREE_PAGE_HEADER_SIZE_OFFSET);
    REQUIRE(header_size.ok());
    REQUIRE(header_size.value() == BTREE_PAGE_HEADER_SIZE);

    const auto parent_page_id = read_u64_le(bytes, BTREE_PAGE_PARENT_PAGE_ID_OFFSET);
    REQUIRE(parent_page_id.ok());
    REQUIRE(parent_page_id.value() == INVALID_PAGE_ID.id);

    const auto next_leaf_page_id = read_u64_le(bytes, BTREE_PAGE_NEXT_LEAF_PAGE_ID_OFFSET);
    REQUIRE(next_leaf_page_id.ok());
    REQUIRE(next_leaf_page_id.value() == INVALID_PAGE_ID.id);

    const auto previous_leaf_page_id = read_u64_le(bytes, BTREE_PAGE_PREVIOUS_LEAF_PAGE_ID_OFFSET);
    REQUIRE(previous_leaf_page_id.ok());
    REQUIRE(previous_leaf_page_id.value() == INVALID_PAGE_ID.id);

    const auto first_child_page_id = read_u64_le(bytes, BTREE_PAGE_FIRST_CHILD_PAGE_ID_OFFSET);
    REQUIRE(first_child_page_id.ok());
    REQUIRE(first_child_page_id.value() == INVALID_PAGE_ID.id);

    const auto key_size = read_u16_le(bytes, BTREE_PAGE_KEY_SIZE_OFFSET);
    REQUIRE(key_size.ok());
    REQUIRE(key_size.value() == 8);

    const auto value_size = read_u16_le(bytes, BTREE_PAGE_VALUE_SIZE_OFFSET);
    REQUIRE(value_size.ok());
    REQUIRE(value_size.value() == 32);

    REQUIRE(bytes[6] == std::byte{ 0 });
    REQUIRE(bytes[7] == std::byte{ 0 });
    for(std::size_t offset = 44; offset < BTREE_PAGE_HEADER_SIZE; offset++) {
        REQUIRE(bytes[offset] == std::byte{ 0 });
    }
}

TEST_CASE("B+ tree internal initialization writes the documented page kind", "[btree][page]") {
    const auto bytes = make_internal_page(16, 64);

    REQUIRE(bytes[BTREE_PAGE_KIND_OFFSET] == static_cast<std::byte>(BTREE_INTERNAL_PAGE_KIND));
    REQUIRE(validate(bytes).ok());
}

TEST_CASE("B+ tree page view reads initialized leaf header fields", "[btree][page]") {
    auto bytes = make_leaf_page();

    const auto result = BTreePage<std::byte>::open(bytes);

    REQUIRE(result.ok());

    const auto page = result.value();

    REQUIRE(page.kind() == BTreePageKind::Leaf);
    REQUIRE_FALSE(page.is_root());
    REQUIRE(page.key_count() == 0);
    REQUIRE(page.parent_page_id() == INVALID_PAGE_ID);
    REQUIRE(page.next_leaf_page_id() == INVALID_PAGE_ID);
    REQUIRE(page.previous_leaf_page_id() == INVALID_PAGE_ID);
    REQUIRE(page.first_child_page_id() == INVALID_PAGE_ID);
    REQUIRE(page.key_size() == 8);
    REQUIRE(page.value_size() == 32);
    REQUIRE(page.leaf_entry_size() == 40);
    REQUIRE(page.internal_entry_size() == 16);
    REQUIRE(page.leaf_capacity() == BTREE_PAGE_ENTRY_AREA_SIZE / 40);
    REQUIRE(page.internal_capacity() == BTREE_PAGE_ENTRY_AREA_SIZE / 16);
}

TEST_CASE("B+ tree page view reads initialized internal header fields", "[btree][page]") {
    auto bytes = make_internal_page(16, 64);

    std::span<const std::byte> const_bytes{ bytes };
    const auto result = BTreePage<const std::byte>::open(const_bytes);

    REQUIRE(result.ok());

    const auto page = result.value();

    REQUIRE(page.kind() == BTreePageKind::Internal);
    REQUIRE(page.key_count() == 0);
    REQUIRE(page.key_size() == 16);
    REQUIRE(page.value_size() == 64);
    REQUIRE(page.leaf_entry_size() == 80);
    REQUIRE(page.internal_entry_size() == 24);
    REQUIRE(page.leaf_capacity() == BTREE_PAGE_ENTRY_AREA_SIZE / 80);
    REQUIRE(page.internal_capacity() == BTREE_PAGE_ENTRY_AREA_SIZE / 24);
}

TEST_CASE("B+ tree page computes capacity for a string-heavy table leaf", "[btree][page]") {
    constexpr std::uint16_t KEY_SIZE = 8;
    constexpr std::uint16_t ROW_SIZE = 169;

    auto bytes = make_leaf_page(KEY_SIZE, ROW_SIZE);

    const auto result = BTreePage<std::byte>::open(bytes);

    REQUIRE(result.ok());

    const auto page = result.value();

    REQUIRE(page.leaf_entry_size() == 177);
    REQUIRE(page.leaf_capacity() == 22);
    REQUIRE(page.internal_entry_size() == 16);
    REQUIRE(page.internal_capacity() == 252);
}

TEST_CASE("B+ tree page setters update mutable header fields", "[btree][page]") {
    auto bytes = make_leaf_page();

    auto result = BTreePage<std::byte>::open(bytes);
    REQUIRE(result.ok());

    auto page = result.value();

    REQUIRE(page.set_key_count(3).ok());
    page.set_parent_page_id(PageId{ 7 });
    page.set_next_leaf_page_id(PageId{ 8 });
    page.set_previous_leaf_page_id(PageId{ 6 });
    page.set_first_child_page_id(PageId{ 9 });
    page.set_root(true);

    REQUIRE(page.key_count() == 3);
    REQUIRE(page.parent_page_id() == PageId{ 7 });
    REQUIRE(page.next_leaf_page_id() == PageId{ 8 });
    REQUIRE(page.previous_leaf_page_id() == PageId{ 6 });
    REQUIRE(page.first_child_page_id() == PageId{ 9 });
    REQUIRE(page.is_root());

    page.set_root(false);

    REQUIRE_FALSE(page.is_root());
}

TEST_CASE("B+ tree page rejects key counts above page capacity", "[btree][page]") {
    auto bytes = make_leaf_page();

    auto result = BTreePage<std::byte>::open(bytes);
    REQUIRE(result.ok());

    auto page = result.value();
    const auto too_many_keys = static_cast<std::uint16_t>(page.leaf_capacity() + 1);

    const auto status = page.set_key_count(too_many_keys);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InvalidArgument);
    REQUIRE(page.key_count() == 0);
}

TEST_CASE("B+ tree page initialization rejects impossible layouts", "[btree][page]") {
    SECTION("wrong page size") {
        std::array<std::byte, PAGE_SIZE - 1> bytes{};

        const auto status = initialize_leaf(bytes, 8, 32);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("zero key size") {
        PageBytes bytes{};

        const auto status = initialize_leaf(bytes, 0, 32);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("zero value size") {
        PageBytes bytes{};

        const auto status = initialize_leaf(bytes, 8, 0);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("leaf entry cannot fit even once") {
        PageBytes bytes{};

        const auto status = initialize_leaf(bytes, 8, BTREE_PAGE_ENTRY_AREA_SIZE);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }

    SECTION("internal entry cannot fit even once") {
        PageBytes bytes{};

        const auto status = initialize_internal(bytes, BTREE_PAGE_ENTRY_AREA_SIZE, 8);

        REQUIRE_FALSE(status.ok());
        REQUIRE(status.code() == StatusCode::InvalidArgument);
    }
}

TEST_CASE("B+ tree page initialization accepts a one-entry layout", "[btree][page]") {
    PageBytes bytes{};

    const auto status = initialize_leaf(bytes, 8, BTREE_PAGE_ENTRY_AREA_SIZE - 8);

    REQUIRE(status.ok());

    const auto result = BTreePage<std::byte>::open(bytes);

    REQUIRE(result.ok());
    REQUIRE(result.value().leaf_capacity() == 1);
}

TEST_CASE("B+ tree page validation rejects invalid fixed header fields", "[btree][page]") {
    SECTION("bad page kind") {
        auto bytes = make_leaf_page();
        bytes[BTREE_PAGE_KIND_OFFSET] = std::byte{ 0 };

        require_corruption(bytes);
    }

    SECTION("unsupported flag bits") {
        auto bytes = make_leaf_page();
        bytes[BTREE_PAGE_FLAGS_OFFSET] = std::byte{ BTREE_PAGE_ROOT_FLAG | 0x02 };

        require_corruption(bytes);
    }

    SECTION("bad header size") {
        auto bytes = make_leaf_page();
        REQUIRE(write_u16_le(bytes, BTREE_PAGE_HEADER_SIZE_OFFSET, BTREE_PAGE_HEADER_SIZE + 1).ok());

        require_corruption(bytes);
    }

    SECTION("first reserved range is non-zero") {
        auto bytes = make_leaf_page();
        bytes[6] = std::byte{ 0x01 };

        require_corruption(bytes);
    }

    SECTION("second reserved range is non-zero") {
        auto bytes = make_leaf_page();
        bytes[44] = std::byte{ 0x01 };

        require_corruption(bytes);
    }

    SECTION("zero key size") {
        auto bytes = make_leaf_page();
        REQUIRE(write_u16_le(bytes, BTREE_PAGE_KEY_SIZE_OFFSET, 0).ok());

        require_corruption(bytes);
    }

    SECTION("zero value size") {
        auto bytes = make_leaf_page();
        REQUIRE(write_u16_le(bytes, BTREE_PAGE_VALUE_SIZE_OFFSET, 0).ok());

        require_corruption(bytes);
    }

    SECTION("entry size is too large") {
        auto bytes = make_leaf_page();
        REQUIRE(write_u16_le(bytes, BTREE_PAGE_VALUE_SIZE_OFFSET, BTREE_PAGE_ENTRY_AREA_SIZE).ok());

        require_corruption(bytes);
    }

    SECTION("key count exceeds capacity") {
        auto bytes = make_leaf_page();
        REQUIRE(write_u16_le(bytes, BTREE_PAGE_KEY_COUNT_OFFSET, 101).ok());

        require_corruption(bytes);
    }
}

TEST_CASE("B+ tree page open returns validation errors", "[btree][page]") {
    auto bytes = make_leaf_page();
    bytes[BTREE_PAGE_KIND_OFFSET] = std::byte{ 0 };

    const auto result = BTreePage<std::byte>::open(bytes);

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == StatusCode::Corruption);
}
