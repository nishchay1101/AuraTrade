#pragma once

#include "Interfaces.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace AuraTrade {

// Writes ExecutionReport records to a memory-mapped file for near-zero-latency durability.
// The file is pre-allocated to `capacity` bytes.  When the capacity is reached
// further writes are silently dropped (production systems would rotate the file).
class BinaryLogger final : public IPersistenceHandler {
public:
    explicit BinaryLogger(const std::string& path,
                          std::size_t capacity = 64ULL * 1024 * 1024)
        : capacity_(capacity) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0)
            throw std::runtime_error("BinaryLogger: cannot open " + path);

        if (::ftruncate(fd_, static_cast<off_t>(capacity_)) < 0) {
            ::close(fd_);
            throw std::runtime_error("BinaryLogger: ftruncate failed");
        }

        data_ = static_cast<std::byte*>(
            ::mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (data_ == MAP_FAILED) {
            ::close(fd_);
            throw std::runtime_error("BinaryLogger: mmap failed");
        }
    }

    ~BinaryLogger() override {
        if (data_) ::munmap(data_, capacity_);
        if (fd_ >= 0) ::close(fd_);
    }

    BinaryLogger(const BinaryLogger&)            = delete;
    BinaryLogger& operator=(const BinaryLogger&) = delete;

    void persist(const ExecutionReport& report) override {
        if (offset_ + sizeof(ExecutionReport) > capacity_) return;
        std::memcpy(data_ + offset_, &report, sizeof(ExecutionReport));
        offset_ += sizeof(ExecutionReport);
    }

private:
    int         fd_       = -1;
    std::size_t capacity_ = 0;
    std::size_t offset_   = 0;
    std::byte*  data_     = nullptr;
};

} // namespace AuraTrade
