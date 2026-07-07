#pragma once

#include <dandb/btree/BTreePage.h>

#include <cstdint>
#include <utility>

namespace dandb::btree {

    template<BTreePageByte Byte>
    class BTreeInternalPage {
        public:

            static core::Result<BTreeInternalPage<Byte>> open(std::span<Byte> bytes);

            BTreePageKind kind() const;
            bool is_root() const;
            std::uint16_t key_count() const;
            storage::PageId parent_page_id() const;
            storage::PageId first_child_page_id() const;
            std::uint16_t key_size() const;
            std::uint16_t value_size() const;

            std::size_t entry_size() const;
            std::size_t capacity() const;

            core::Status set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>);
            void set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>);
            void set_first_child_page_id(storage::PageId first_child_page_id) requires (!std::is_const_v<Byte>);
            void set_root(bool is_root) requires (!std::is_const_v<Byte>);

        private:
            explicit BTreeInternalPage(BTreePage<Byte> page);

            BTreePage<Byte> page_;
    };

    template<BTreePageByte Byte>
    core::Result<BTreeInternalPage<Byte>> BTreeInternalPage<Byte>::open(std::span<Byte> bytes) {

        auto page_result = BTreePage<Byte>::open(bytes);
        if(!page_result.ok()) {
            return page_result.status();
        }

        auto& page = page_result.value();
        if(page.kind() != BTreePageKind::Internal) {
            return core::Status::InvalidArgument("Cannot open B+ tree internal page: page kind is not internal");
        }

        return BTreeInternalPage<Byte>{ std::move(page) };

    }

    template<BTreePageByte Byte>
    BTreeInternalPage<Byte>::BTreeInternalPage(BTreePage<Byte> page) :
        page_(std::move(page))
    {}

    template<BTreePageByte Byte>
    BTreePageKind BTreeInternalPage<Byte>::kind() const {

        return page_.kind();

    }

    template<BTreePageByte Byte>
    bool BTreeInternalPage<Byte>::is_root() const {

        return page_.is_root();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeInternalPage<Byte>::key_count() const {

        return page_.key_count();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreeInternalPage<Byte>::parent_page_id() const {

        return page_.parent_page_id();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreeInternalPage<Byte>::first_child_page_id() const {

        return page_.first_child_page_id();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeInternalPage<Byte>::key_size() const {

        return page_.key_size();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeInternalPage<Byte>::value_size() const {

        return page_.value_size();

    }

    template<BTreePageByte Byte>
    std::size_t BTreeInternalPage<Byte>::entry_size() const {

        return static_cast<std::size_t>(key_size())+sizeof(std::uint64_t);

    }

    template<BTreePageByte Byte>
    std::size_t BTreeInternalPage<Byte>::capacity() const {

        return BTREE_PAGE_ENTRY_AREA_SIZE/entry_size();

    }

    template<BTreePageByte Byte>
    core::Status BTreeInternalPage<Byte>::set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>) {

        if(key_count > capacity()) {
            return core::Status::InvalidArgument("Cannot set B+ tree page key count: key count exceeds page capacity");
        }

        return page_.set_key_count(key_count);

    }

    template<BTreePageByte Byte>
    void BTreeInternalPage<Byte>::set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>) {

        page_.set_parent_page_id(parent_page_id);

    }

    template<BTreePageByte Byte>
    void BTreeInternalPage<Byte>::set_first_child_page_id(storage::PageId first_child_page_id) requires (!std::is_const_v<Byte>) {

        page_.set_first_child_page_id(first_child_page_id);

    }

    template<BTreePageByte Byte>
    void BTreeInternalPage<Byte>::set_root(bool is_root) requires (!std::is_const_v<Byte>) {

        page_.set_root(is_root);

    }

}
