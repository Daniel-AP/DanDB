#pragma once

#include <dandb/btree/BTreePage.h>

#include <cstring>
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
            core::Result<std::span<const std::byte>> key_at(std::uint16_t entry_index) const;
            core::Result<storage::PageId> right_child_page_id_at(std::uint16_t entry_index) const;
            core::Result<std::uint16_t> find_insertion_position(std::span<const std::byte> key) const;

            core::Status insert_entry(
                std::uint16_t entry_index,
                std::span<const std::byte> key,
                storage::PageId right_child_page_id
            ) requires (!std::is_const_v<Byte>);
            core::Status erase_entry(std::uint16_t entry_index) requires (!std::is_const_v<Byte>);

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
    core::Result<std::span<const std::byte>> BTreeInternalPage<Byte>::key_at(std::uint16_t entry_index) const {

        if(entry_index >= key_count()) {
            return core::Status::InvalidArgument("Cannot read B+ tree internal page key: entry index is out of bounds");
        }

        const auto entry_offset = BTREE_PAGE_ENTRY_ARRAY_OFFSET+static_cast<std::size_t>(entry_index)*entry_size();
        return std::span<const std::byte>{ page_.bytes_ }.subspan(entry_offset, key_size());

    }

    template<BTreePageByte Byte>
    core::Result<storage::PageId> BTreeInternalPage<Byte>::right_child_page_id_at(std::uint16_t entry_index) const {

        if(entry_index >= key_count()) {
            return core::Status::InvalidArgument("Cannot read B+ tree internal page right child page id: entry index is out of bounds");
        }

        const auto entry_offset = BTREE_PAGE_ENTRY_ARRAY_OFFSET+static_cast<std::size_t>(entry_index)*entry_size();
        const auto right_child_page_id_offset = entry_offset+key_size();
        const auto result = core::read_u64_le(page_.bytes_, right_child_page_id_offset);
        if(!result.ok()) {
            return result.status();
        }

        return storage::PageId{ result.value() };

    }

    template<BTreePageByte Byte>
    core::Result<std::uint16_t> BTreeInternalPage<Byte>::find_insertion_position(std::span<const std::byte> key) const {

        if(key.size() != key_size()) {
            return core::Status::InvalidArgument("Cannot find B+ tree internal page insertion position: key size is invalid");
        }

        std::uint16_t left = 0;
        std::uint16_t right = key_count();

        while(left < right) {
            std::uint16_t mid = left+(right-left)/2;

            auto stored_key_result = key_at(mid);
            if(!stored_key_result.ok()) {
                return stored_key_result.status();
            }

            const auto stored_key = stored_key_result.value();
            if(std::memcmp(stored_key.data(), key.data(), key.size()) < 0) {
                left = mid+1;
            } else {
                right = mid;
            }
        }

        return left;

    }

    template<BTreePageByte Byte>
    core::Status BTreeInternalPage<Byte>::insert_entry(
        std::uint16_t entry_index,
        std::span<const std::byte> key,
        storage::PageId right_child_page_id
    ) requires (!std::is_const_v<Byte>) {

        const auto stored_key_count = key_count();
        if(entry_index > stored_key_count) {
            return core::Status::InvalidArgument("Cannot insert B+ tree internal page entry: entry index is out of bounds");
        }

        if(key.size() != key_size()) {
            return core::Status::InvalidArgument("Cannot insert B+ tree internal page entry: key size is invalid");
        }

        if(stored_key_count >= capacity()) {
            return core::Status::InvalidArgument("Cannot insert B+ tree internal page entry: page is full");
        }

        const auto entry_offset = BTREE_PAGE_ENTRY_ARRAY_OFFSET+static_cast<std::size_t>(entry_index)*entry_size();
        const auto bytes_to_shift = static_cast<std::size_t>(stored_key_count-entry_index)*entry_size();
        if(bytes_to_shift > 0) {
            std::memmove(
                page_.bytes_.data()+entry_offset+entry_size(),
                page_.bytes_.data()+entry_offset,
                bytes_to_shift
            );
        }

        std::memcpy(page_.bytes_.data()+entry_offset, key.data(), key.size());
        auto status = core::write_u64_le(page_.bytes_, entry_offset+key_size(), right_child_page_id.id);
        if(!status.ok()) {
            return status;
        }

        return set_key_count(static_cast<std::uint16_t>(stored_key_count+1));

    }

    template<BTreePageByte Byte>
    core::Status BTreeInternalPage<Byte>::erase_entry(std::uint16_t entry_index) requires (!std::is_const_v<Byte>) {

        const auto stored_key_count = key_count();
        if(entry_index >= stored_key_count) {
            return core::Status::InvalidArgument("Cannot erase B+ tree internal page entry: entry index is out of bounds");
        }

        const auto entry_offset = BTREE_PAGE_ENTRY_ARRAY_OFFSET+static_cast<std::size_t>(entry_index)*entry_size();
        const auto bytes_to_shift = static_cast<std::size_t>(stored_key_count-entry_index-1)*entry_size();
        if(bytes_to_shift > 0) {
            std::memmove(
                page_.bytes_.data()+entry_offset,
                page_.bytes_.data()+entry_offset+entry_size(),
                bytes_to_shift
            );
        }

        return set_key_count(static_cast<std::uint16_t>(stored_key_count-1));

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
