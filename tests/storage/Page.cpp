#include <catch_amalgamated.hpp>

#include <dandb/core/Constants.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>

#include <cstddef>

using dandb::core::PAGE_SIZE;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::Page;
using dandb::storage::PageId;

TEST_CASE("Page starts as an empty zeroed storage page", "[storage][page]") {
    Page page{};

    REQUIRE(page.id() == INVALID_PAGE_ID);
    REQUIRE(page.data().size() == PAGE_SIZE);

    for(const auto byte : page.data()) {
        REQUIRE(byte == std::byte{ 0 });
    }
}

TEST_CASE("Page can be created with a real page id", "[storage][page]") {
    const Page page{ PageId{ 7 } };

    REQUIRE(page.id() == PageId{ 7 });
    REQUIRE(page.data().size() == PAGE_SIZE);
}

TEST_CASE("Page id can be changed after creation", "[storage][page]") {
    Page page{};

    page.set_id(PageId{ 42 });

    REQUIRE(page.id() == PageId{ 42 });
}

TEST_CASE("Page exposes mutable fixed-size bytes", "[storage][page]") {
    Page page{};

    page.data()[0] = std::byte{ 0x12 };
    page.data()[PAGE_SIZE - 1] = std::byte{ 0x34 };

    REQUIRE(page.data()[0] == std::byte{ 0x12 });
    REQUIRE(page.data()[PAGE_SIZE - 1] == std::byte{ 0x34 });
}

TEST_CASE("Page exposes read-only bytes from const pages", "[storage][page]") {
    Page page{};
    page.data()[3] = std::byte{ 0x56 };

    const Page& const_page = page;

    REQUIRE(const_page.data().size() == PAGE_SIZE);
    REQUIRE(const_page.data()[3] == std::byte{ 0x56 });
}
