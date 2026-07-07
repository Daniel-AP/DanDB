#pragma once

#include <dandb/btree/BTreePage.h>

#include <utility>

namespace dandb::btree {

    template<BTreePageByte Byte>
    class BTreeLeafPage {
        public:

            static core::Result<BTreeLeafPage<Byte>> open(std::span<Byte> bytes);

            BTreePageKind kind() const;
            bool is_root() const;
            std::uint16_t key_count() const;
            storage::PageId parent_page_id() const;
            storage::PageId next_leaf_page_id() const;
            storage::PageId previous_leaf_page_id() const;
            std::uint16_t key_size() const;
            std::uint16_t value_size() const;

            std::size_t entry_size() const;
            std::size_t capacity() const;

            core::Status set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>);
            void set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>);
            void set_next_leaf_page_id(storage::PageId next_leaf_page_id) requires (!std::is_const_v<Byte>);
            void set_previous_leaf_page_id(storage::PageId previous_leaf_page_id) requires (!std::is_const_v<Byte>);
            void set_root(bool is_root) requires (!std::is_const_v<Byte>);

        private:
            explicit BTreeLeafPage(BTreePage<Byte> page);

            BTreePage<Byte> page_;
    };

    template<BTreePageByte Byte>
    core::Result<BTreeLeafPage<Byte>> BTreeLeafPage<Byte>::open(std::span<Byte> bytes) {

        auto page_result = BTreePage<Byte>::open(bytes);
        if(!page_result.ok()) {
            return page_result.status();
        }

        auto& page = page_result.value();
        if(page.kind() != BTreePageKind::Leaf) {
            return core::Status::InvalidArgument("Cannot open B+ tree leaf page: page kind is not leaf");
        }

        return BTreeLeafPage<Byte>{ std::move(page) };

    }

    template<BTreePageByte Byte>
    BTreeLeafPage<Byte>::BTreeLeafPage(BTreePage<Byte> page) :
        page_(std::move(page))
    {}

    template<BTreePageByte Byte>
    BTreePageKind BTreeLeafPage<Byte>::kind() const {

        return page_.kind();

    }

    template<BTreePageByte Byte>
    bool BTreeLeafPage<Byte>::is_root() const {

        return page_.is_root();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeLeafPage<Byte>::key_count() const {

        return page_.key_count();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreeLeafPage<Byte>::parent_page_id() const {

        return page_.parent_page_id();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreeLeafPage<Byte>::next_leaf_page_id() const {

        return page_.next_leaf_page_id();

    }

    template<BTreePageByte Byte>
    storage::PageId BTreeLeafPage<Byte>::previous_leaf_page_id() const {

        return page_.previous_leaf_page_id();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeLeafPage<Byte>::key_size() const {

        return page_.key_size();

    }

    template<BTreePageByte Byte>
    std::uint16_t BTreeLeafPage<Byte>::value_size() const {

        return page_.value_size();

    }

    template<BTreePageByte Byte>
    std::size_t BTreeLeafPage<Byte>::entry_size() const {

        return static_cast<std::size_t>(key_size())+value_size();

    }

    template<BTreePageByte Byte>
    std::size_t BTreeLeafPage<Byte>::capacity() const {

        return BTREE_PAGE_ENTRY_AREA_SIZE/entry_size();

    }

    template<BTreePageByte Byte>
    core::Status BTreeLeafPage<Byte>::set_key_count(std::uint16_t key_count) requires (!std::is_const_v<Byte>) {

        if(key_count > capacity()) {
            return core::Status::InvalidArgument("Cannot set B+ tree page key count: key count exceeds page capacity");
        }

        return page_.set_key_count(key_count);

    }

    template<BTreePageByte Byte>
    void BTreeLeafPage<Byte>::set_parent_page_id(storage::PageId parent_page_id) requires (!std::is_const_v<Byte>) {

        page_.set_parent_page_id(parent_page_id);

    }

    template<BTreePageByte Byte>
    void BTreeLeafPage<Byte>::set_next_leaf_page_id(storage::PageId next_leaf_page_id) requires (!std::is_const_v<Byte>) {

        page_.set_next_leaf_page_id(next_leaf_page_id);

    }

    template<BTreePageByte Byte>
    void BTreeLeafPage<Byte>::set_previous_leaf_page_id(storage::PageId previous_leaf_page_id) requires (!std::is_const_v<Byte>) {

        page_.set_previous_leaf_page_id(previous_leaf_page_id);

    }

    template<BTreePageByte Byte>
    void BTreeLeafPage<Byte>::set_root(bool is_root) requires (!std::is_const_v<Byte>) {

        page_.set_root(is_root);

    }

}
