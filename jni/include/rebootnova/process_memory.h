#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace rebootnova {

struct MemoryRange {
    std::uintptr_t start{};
    std::uintptr_t end{};
    bool writable{};
    bool executable{};
    bool private_mapping{};
    std::string path;
};

struct MemoryRead {
    std::uintptr_t address{};
    void* output{};
    std::size_t size{};
};

class ProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory();

    bool attach_by_package(std::string_view package);
    bool refresh_module(std::string_view module);
    bool refresh_module_candidates(const std::vector<std::string>& modules);
    void detach();
    bool alive() const;

    pid_t pid() const { return pid_.load(std::memory_order_acquire); }
    std::uintptr_t module_base() const { return module_base_.load(std::memory_order_acquire); }
    std::string module_name() const;
    std::string last_error() const;

    template <typename T>
    std::optional<T> read(std::uintptr_t address) const {
        T value{};
        if (!read_bytes(address, &value, sizeof(value))) return std::nullopt;
        return value;
    }

    bool read_bytes(std::uintptr_t address, void* out, std::size_t size) const;
    bool read_scatter(const std::vector<MemoryRead>& reads) const;
    bool write_bytes(std::uintptr_t address, const void* data, std::size_t size) const;
    bool valid_pointer(std::uintptr_t address) const;
    std::optional<MemoryRange> mapping_for(std::uintptr_t address) const;
    std::vector<MemoryRange> readable_mappings() const;
    std::vector<MemoryRange> readable_mappings(pid_t target_pid) const;

private:
    std::atomic<pid_t> pid_{-1};
    std::atomic<std::uintptr_t> module_base_{0};
    std::string module_name_;
    mutable int mem_fd_{-1};
    mutable std::string last_error_;
    mutable std::mutex mutex_;

    bool alive_unlocked() const;
    void detach_unlocked();
    bool valid_address_range(std::uintptr_t address, std::size_t size) const;
    bool open_mem_fallback() const;
    bool open_mem_writable() const;
    void set_error(const std::string& error) const;
};

}  // namespace rebootnova
