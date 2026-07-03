#include <catch_amalgamated.hpp>

#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/buffer/PagePin.h>
#include <dandb/core/Status.h>
#include <dandb/storage/Page.h>
#include <dandb/storage/PageId.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using dandb::buffer::BufferPoolManager;
using dandb::buffer::PagePin;
using dandb::core::StatusCode;
using dandb::storage::INVALID_PAGE_ID;
using dandb::storage::Page;
using dandb::storage::PageId;

namespace {

    Page make_page(std::uint64_t page_id, std::byte marker) {
        Page page{ PageId{ page_id } };
        page.data()[0] = marker;
        return page;
    }

}

TEST_CASE("BufferPoolManager reports not found for pages that are not cached", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 2 };

    const auto page = buffer_pool.get_page(PageId{ 7 });

    REQUIRE_FALSE(page.ok());
    REQUIRE(page.status().code() == StatusCode::NotFound);
}

TEST_CASE("BufferPoolManager rejects invalid page ids", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 2 };

    const auto get_result = buffer_pool.get_page(INVALID_PAGE_ID);
    REQUIRE_FALSE(get_result.ok());
    REQUIRE(get_result.status().code() == StatusCode::InvalidArgument);

    const auto cache_result = buffer_pool.cache_page(Page{});
    REQUIRE_FALSE(cache_result.ok());
    REQUIRE(cache_result.status().code() == StatusCode::InvalidArgument);
}

TEST_CASE("BufferPoolManager caches a page and returns the cached bytes", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 2 };
    Page page = make_page(7, std::byte{ 0x42 });

    {
        auto cached = buffer_pool.cache_page(page);

        REQUIRE(cached.ok());
        REQUIRE(cached.value().page() != nullptr);
        REQUIRE(cached.value().page()->id() == PageId{ 7 });
        REQUIRE(cached.value().page()->data()[0] == std::byte{ 0x42 });
    }

    auto fetched = buffer_pool.get_page(PageId{ 7 });

    REQUIRE(fetched.ok());
    REQUIRE(fetched.value().page() != nullptr);
    REQUIRE(fetched.value().page()->id() == PageId{ 7 });
    REQUIRE(fetched.value().page()->data()[0] == std::byte{ 0x42 });
}

TEST_CASE("BufferPoolManager rejects caching the same page twice", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 2 };
    Page page = make_page(7, std::byte{ 0x42 });

    auto cached = buffer_pool.cache_page(page);
    REQUIRE(cached.ok());

    const auto duplicate = buffer_pool.cache_page(page);

    REQUIRE_FALSE(duplicate.ok());
    REQUIRE(duplicate.status().code() == StatusCode::AlreadyExists);
}

TEST_CASE("PagePin releases a clean page so the frame can be reused", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 1 };

    {
        auto cached = buffer_pool.cache_page(make_page(1, std::byte{ 0x11 }));
        REQUIRE(cached.ok());
    }

    auto replacement = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

    REQUIRE(replacement.ok());
    REQUIRE(replacement.value().page() != nullptr);
    REQUIRE(replacement.value().page()->id() == PageId{ 2 });
    REQUIRE(replacement.value().page()->data()[0] == std::byte{ 0x22 });

    const auto old_page = buffer_pool.get_page(PageId{ 1 });
    REQUIRE_FALSE(old_page.ok());
    REQUIRE(old_page.status().code() == StatusCode::NotFound);
}

TEST_CASE("BufferPoolManager does not evict a pinned page", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 1 };

    {
        auto pinned = buffer_pool.cache_page(make_page(1, std::byte{ 0x11 }));
        REQUIRE(pinned.ok());

        const auto replacement = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

        REQUIRE_FALSE(replacement.ok());
        REQUIRE(replacement.status().code() == StatusCode::NotFound);
        REQUIRE(pinned.value().page() != nullptr);
        REQUIRE(pinned.value().page()->id() == PageId{ 1 });
        REQUIRE(pinned.value().page()->data()[0] == std::byte{ 0x11 });
    }

    auto replacement_after_release = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

    REQUIRE(replacement_after_release.ok());
    REQUIRE(replacement_after_release.value().page() != nullptr);
    REQUIRE(replacement_after_release.value().page()->id() == PageId{ 2 });
}

TEST_CASE("BufferPoolManager does not evict a dirty page", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 1 };

    {
        auto cached = buffer_pool.cache_page(make_page(1, std::byte{ 0x11 }));
        REQUIRE(cached.ok());
        cached.value().mark_dirty();
    }

    const auto replacement = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

    REQUIRE_FALSE(replacement.ok());
    REQUIRE(replacement.status().code() == StatusCode::NotFound);

    auto original = buffer_pool.get_page(PageId{ 1 });
    REQUIRE(original.ok());
    REQUIRE(original.value().page() != nullptr);
    REQUIRE(original.value().page()->id() == PageId{ 1 });
    REQUIRE(original.value().page()->data()[0] == std::byte{ 0x11 });
}

TEST_CASE("Moving a PagePin transfers the pin", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 1 };

    {
        auto cached = buffer_pool.cache_page(make_page(1, std::byte{ 0x11 }));
        REQUIRE(cached.ok());

        PagePin guard = std::move(cached.value());
        PagePin moved_guard = std::move(guard);

        const auto replacement = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

        REQUIRE_FALSE(replacement.ok());
        REQUIRE(replacement.status().code() == StatusCode::NotFound);
        REQUIRE(moved_guard.page() != nullptr);
        REQUIRE(moved_guard.page()->id() == PageId{ 1 });
    }

    auto replacement_after_release = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

    REQUIRE(replacement_after_release.ok());
    REQUIRE(replacement_after_release.value().page() != nullptr);
    REQUIRE(replacement_after_release.value().page()->id() == PageId{ 2 });
}

TEST_CASE("Moving a dirty PagePin preserves dirty state", "[buffer][buffer-pool-manager]") {
    BufferPoolManager buffer_pool{ 1 };

    {
        auto cached = buffer_pool.cache_page(make_page(1, std::byte{ 0x11 }));
        REQUIRE(cached.ok());
        cached.value().mark_dirty();

        PagePin moved_guard = std::move(cached.value());
        REQUIRE(moved_guard.is_dirty());
    }

    const auto replacement = buffer_pool.cache_page(make_page(2, std::byte{ 0x22 }));

    REQUIRE_FALSE(replacement.ok());
    REQUIRE(replacement.status().code() == StatusCode::NotFound);
}
