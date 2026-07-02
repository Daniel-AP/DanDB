#include <catch_amalgamated.hpp>

#include <dandb/buffer/BufferFrame.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>

#include <cstddef>

using dandb::buffer::BufferFrame;
using dandb::core::StatusCode;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::Page;
using dandb::storage::PageId;

TEST_CASE("BufferFrame starts with an empty clean unpinned page", "[buffer][buffer-frame]") {
    BufferFrame frame{};

    REQUIRE(frame.page().id() == INVALID_PAGE_ID);
    REQUIRE(frame.pin_count() == 0);
    REQUIRE_FALSE(frame.is_pinned());
    REQUIRE_FALSE(frame.is_dirty());

    for(const auto byte : frame.page().data()) {
        REQUIRE(byte == std::byte{ 0 });
    }
}

TEST_CASE("BufferFrame can be created around an existing page", "[buffer][buffer-frame]") {
    Page page{ PageId{ 7 } };
    page.data()[3] = std::byte{ 0x42 };

    BufferFrame frame{ page };

    REQUIRE(frame.page().id() == PageId{ 7 });
    REQUIRE(frame.page().data()[3] == std::byte{ 0x42 });
    REQUIRE(frame.pin_count() == 0);
    REQUIRE_FALSE(frame.is_dirty());
}

TEST_CASE("BufferFrame exposes mutable page bytes", "[buffer][buffer-frame]") {
    BufferFrame frame{};

    frame.page().set_id(PageId{ 11 });
    frame.page().data()[0] = std::byte{ 0x12 };

    REQUIRE(frame.page().id() == PageId{ 11 });
    REQUIRE(frame.page().data()[0] == std::byte{ 0x12 });
}

TEST_CASE("BufferFrame pin and unpin update pin state", "[buffer][buffer-frame]") {
    BufferFrame frame{};

    frame.pin();
    frame.pin();

    REQUIRE(frame.pin_count() == 2);
    REQUIRE(frame.is_pinned());

    REQUIRE(frame.unpin().ok());

    REQUIRE(frame.pin_count() == 1);
    REQUIRE(frame.is_pinned());

    REQUIRE(frame.unpin().ok());

    REQUIRE(frame.pin_count() == 0);
    REQUIRE_FALSE(frame.is_pinned());
}

TEST_CASE("BufferFrame rejects unpinning when pin count is already zero", "[buffer][buffer-frame]") {
    BufferFrame frame{};

    const auto status = frame.unpin();

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == StatusCode::InternalError);
    REQUIRE(frame.pin_count() == 0);
    REQUIRE_FALSE(frame.is_pinned());
}

TEST_CASE("BufferFrame tracks dirty state", "[buffer][buffer-frame]") {
    BufferFrame frame{};

    frame.mark_dirty();

    REQUIRE(frame.is_dirty());

    frame.clear_dirty();

    REQUIRE_FALSE(frame.is_dirty());
}

TEST_CASE("BufferFrame reset clears page and frame state", "[buffer][buffer-frame]") {
    BufferFrame frame{ Page{ PageId{ 99 } } };
    frame.page().data()[0] = std::byte{ 0xAA };
    frame.pin();
    frame.mark_dirty();

    frame.reset();

    REQUIRE(frame.page().id() == INVALID_PAGE_ID);
    REQUIRE(frame.pin_count() == 0);
    REQUIRE_FALSE(frame.is_pinned());
    REQUIRE_FALSE(frame.is_dirty());

    for(const auto byte : frame.page().data()) {
        REQUIRE(byte == std::byte{ 0 });
    }
}
