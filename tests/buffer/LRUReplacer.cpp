#include <catch_amalgamated.hpp>

#include <dandb/buffer/LRUReplacer.h>
#include <dandb/core/Status.h>

#include <cstddef>

using dandb::buffer::LRUReplacer;
using dandb::core::StatusCode;

TEST_CASE("LRUReplacer starts with fixed capacity and no evictable frames", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    REQUIRE(replacer.capacity() == 3);
    REQUIRE(replacer.size() == 0);
}

TEST_CASE("LRUReplacer reports not found when no frame is evictable", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    const auto victim = replacer.victim();

    REQUIRE_FALSE(victim.ok());
    REQUIRE(victim.status().code() == StatusCode::NotFound);
    REQUIRE(replacer.size() == 0);
}

TEST_CASE("LRUReplacer chooses the least recently marked evictable frame", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    REQUIRE(replacer.mark_evictable(0).ok());
    REQUIRE(replacer.mark_evictable(1).ok());
    REQUIRE(replacer.mark_evictable(2).ok());

    REQUIRE(replacer.size() == 3);

    const auto first_victim = replacer.victim();
    REQUIRE(first_victim.ok());
    REQUIRE(first_victim.value() == 0);
    REQUIRE(replacer.size() == 2);

    const auto second_victim = replacer.victim();
    REQUIRE(second_victim.ok());
    REQUIRE(second_victim.value() == 1);
    REQUIRE(replacer.size() == 1);
}

TEST_CASE("LRUReplacer skips frames marked non-evictable", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    REQUIRE(replacer.mark_evictable(0).ok());
    REQUIRE(replacer.mark_evictable(1).ok());
    REQUIRE(replacer.mark_evictable(2).ok());
    REQUIRE(replacer.mark_non_evictable(0).ok());

    REQUIRE(replacer.size() == 2);

    const auto victim = replacer.victim();
    REQUIRE(victim.ok());
    REQUIRE(victim.value() == 1);
    REQUIRE(replacer.size() == 1);
}

TEST_CASE("LRUReplacer can mark a missing frame non-evictable without changing size", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    REQUIRE(replacer.mark_evictable(1).ok());
    REQUIRE(replacer.mark_non_evictable(2).ok());

    REQUIRE(replacer.size() == 1);

    const auto victim = replacer.victim();
    REQUIRE(victim.ok());
    REQUIRE(victim.value() == 1);
}

TEST_CASE("LRUReplacer does not duplicate a frame that is already evictable", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    REQUIRE(replacer.mark_evictable(1).ok());
    REQUIRE(replacer.mark_evictable(1).ok());

    REQUIRE(replacer.size() == 1);

    const auto victim = replacer.victim();
    REQUIRE(victim.ok());
    REQUIRE(victim.value() == 1);
    REQUIRE(replacer.size() == 0);

    const auto empty_victim = replacer.victim();
    REQUIRE_FALSE(empty_victim.ok());
    REQUIRE(empty_victim.status().code() == StatusCode::NotFound);
}

TEST_CASE("LRUReplacer rejects frame ids outside its capacity", "[buffer][lru-replacer]") {
    LRUReplacer replacer{ 3 };

    const auto evictable_status = replacer.mark_evictable(3);
    REQUIRE_FALSE(evictable_status.ok());
    REQUIRE(evictable_status.code() == StatusCode::InvalidArgument);

    const auto non_evictable_status = replacer.mark_non_evictable(3);
    REQUIRE_FALSE(non_evictable_status.ok());
    REQUIRE(non_evictable_status.code() == StatusCode::InvalidArgument);

    REQUIRE(replacer.size() == 0);
}
