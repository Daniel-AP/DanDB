#include <dandb/storage/DiskManager.h>
#include <dandb/platform/FileHandle.h>
#include <dandb/core/Constants.h>
#include <dandb/storage/DatabaseHeader.h>
#include <dandb/storage/Page.h>

#include <utility>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace dandb::storage {

    DiskManager::DiskManager(platform::FileHandle file_handle) : file_handle_(std::move(file_handle)) {}

    core::Result<DiskManager> DiskManager::create_new(std::filesystem::path path, const DatabaseHeader& db_header) {

        if(db_header.page_count() > std::numeric_limits<std::uint64_t>::max()/core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot create new database file: page count is too large to calculate file size");
        }

        std::uint64_t resize_value = db_header.page_count()*core::PAGE_SIZE;
        
        auto file_handle_result = platform::FileHandle::create_new(path);
        if(!file_handle_result.ok()) {
            return file_handle_result.status();
        }

        platform::FileHandle file_handle = std::move(file_handle_result.value());

        auto file_resize_status = file_handle.resize(resize_value);
        if(!file_resize_status.ok()) {
            return file_resize_status;
        }

        DiskManager disk_manager(std::move(file_handle));

        auto write_header_status = disk_manager.write_header(db_header);
        if(!write_header_status.ok()) {
            return write_header_status;
        }
        
        return disk_manager;

    }

    core::Result<DiskManager> DiskManager::open_existing(std::filesystem::path path) {

        auto file_handle_result = platform::FileHandle::open_existing(path);
        if(!file_handle_result.ok()) {
            return file_handle_result.status();
        }

        platform::FileHandle file_handle = std::move(file_handle_result.value());

        auto file_size_result = file_handle.size();
        if(!file_size_result.ok()) {
            return file_size_result.status();
        }

        std::uint64_t file_size = file_size_result.value();
        if(file_size%core::PAGE_SIZE != 0) {
            return core::Status::Corruption("Cannot open existing database file: file size doesn't hold a whole number of pages");
        }

        DiskManager disk_manager(std::move(file_handle));

        auto read_header_result = disk_manager.read_header();
        if(!read_header_result.ok()) {
            return read_header_result.status();
        }

        const auto header = read_header_result.value();
        const std::uint64_t file_page_count = file_size/core::PAGE_SIZE;
        if(header.page_count() != file_page_count) {
            return core::Status::Corruption("Cannot open existing database file: header page count does not match file size");
        }
        
        return disk_manager;

    }

    core::Result<DatabaseHeader> DiskManager::read_header() {

        std::array<std::byte, core::PAGE_SIZE> header_page{};

        auto read_file_status = file_handle_.read_at(0, header_page);
        if(!read_file_status.ok()) {
            return read_file_status;
        }

        auto decode_header_result = DatabaseHeader::decode(header_page);
        if(!decode_header_result.ok()) {
            return decode_header_result.status();
        }

        DatabaseHeader header = std::move(decode_header_result.value());

        return header;

    }

    core::Status DiskManager::write_header(const DatabaseHeader& db_header) {

        std::array<std::byte, core::PAGE_SIZE> header_page{};

        auto db_header_encode_status = db_header.encode_into(header_page);
        if(!db_header_encode_status.ok()) {
            return db_header_encode_status;
        }

        return file_handle_.write_at(0, header_page);

    }
 
    core::Result<Page> DiskManager::read_page(PageId page_id) {

        if(!page_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot read page: page id is invalid");
        }

        if(page_id == HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot read page: page 0 is the database header");
        }

        if(page_id.id > std::numeric_limits<std::uint64_t>::max()/core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot read page: page id is too large to calculate file offset");
        }

        std::uint64_t offset = page_id.id*core::PAGE_SIZE;

        auto file_size_result = size();
        if(!file_size_result.ok()) {
            return file_size_result.status();
        }

        std::uint64_t file_size = file_size_result.value();

        if(file_size < core::PAGE_SIZE || offset > file_size-core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot read page: page is outside the file");
        }

        Page page(page_id);

        auto read_page_status = file_handle_.read_at(offset, page.data());
        if(!read_page_status.ok()) {
            return read_page_status;
        }

        return page;

    }
    
    core::Status DiskManager::write_page(const Page& page) {

        const PageId page_id = page.id();

        if(!page_id.is_valid()) {
            return core::Status::InvalidArgument("Cannot write page: page id is invalid");
        }

        if(page_id == HEADER_PAGE_ID) {
            return core::Status::InvalidArgument("Cannot write page: page 0 is the database header");
        }

        if(page_id.id > std::numeric_limits<std::uint64_t>::max()/core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot write page: page id is too large to calculate file offset");
        }

        std::uint64_t offset = page_id.id*core::PAGE_SIZE;

        auto file_size_result = size();
        if(!file_size_result.ok()) {
            return file_size_result.status();
        }

        std::uint64_t file_size = file_size_result.value();

        if(file_size < core::PAGE_SIZE || offset > file_size-core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot write page: page is outside the file");
        }

        return file_handle_.write_at(offset, page.data());

    }

    core::Status DiskManager::resize_to_page_count(std::uint64_t page_count) {

        if(page_count < INITIAL_DATABASE_PAGE_COUNT) {
            return core::Status::InvalidArgument("Cannot resize database file: page count must include the header page");
        }

        if(page_count > std::numeric_limits<std::uint64_t>::max()/core::PAGE_SIZE) {
            return core::Status::InvalidArgument("Cannot resize database file: page count is too large to calculate file size");
        }

        const std::uint64_t target_size = page_count*core::PAGE_SIZE;

        auto current_size = size();
        if(!current_size.ok()) {
            return current_size.status();
        }

        if(current_size.value() > target_size) {
            return core::Status::InternalError("Cannot resize database file: resizing would shrink the database file");
        }

        if(current_size.value() == target_size) {
            return core::Status::Ok();
        }

        return file_handle_.resize(target_size);

    }

    core::Status DiskManager::sync() {
        return file_handle_.sync();
    }

    core::Status DiskManager::close() {
        return file_handle_.close();
    }

    core::Result<std::uint64_t> DiskManager::size() const {
        return file_handle_.size();
    }

    void DiskManager::set_fault_injector(platform::FileFaultInjector* fault_injector) {
        file_handle_.set_fault_injector(fault_injector);
    }

}
