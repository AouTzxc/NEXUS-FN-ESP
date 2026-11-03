#include "rebootnova/process_memory.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <atomic>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <signal.h>
#include <unistd.h>

#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv 270
#endif
#ifndef __NR_process_vm_writev
#define __NR_process_vm_writev 271
#endif

namespace rebootnova {

ProcessMemory::~ProcessMemory() { detach(); }

void ProcessMemory::set_error(const std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = error;
}

void ProcessMemory::detach_unlocked() {
    if (mem_fd_ >= 0) close(mem_fd_);
    mem_fd_ = -1;
    pid_.store(-1, std::memory_order_release);
    module_base_.store(0, std::memory_order_release);
    module_name_.clear();
}

void ProcessMemory::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    detach_unlocked();
}

bool ProcessMemory::alive_unlocked() const {
    const pid_t pid = pid_.load(std::memory_order_acquire);
    if (pid <= 0) return false;
    return kill(pid, 0) == 0 || errno == EPERM;
}

bool ProcessMemory::alive() const { return alive_unlocked(); }

std::string ProcessMemory::module_name() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return module_name_;
}

std::string ProcessMemory::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

bool ProcessMemory::attach_by_package(std::string_view package) {
    DIR* proc = opendir("/proc");
    if (!proc) { set_error(std::string("opendir /proc: ") + std::strerror(errno)); return false; }
    pid_t found = -1;
    int best_score = -1;
    int process_entries = 0;
    int cmdline_candidates = 0;
    int mapped_package_candidates = 0;
    int maps_open_failures = 0;

    const auto inspect_maps = [&package, &maps_open_failures](const char* pid_name,
                                                               bool& has_unreal,
                                                               bool& maps_package) {
        has_unreal = false;
        maps_package = false;
        std::ifstream maps(std::string("/proc/") + pid_name + "/maps", std::ios::binary);
        if (!maps) {
            ++maps_open_failures;
            return false;
        }
        std::string map_line;
        while (std::getline(maps, map_line)) {
            if (map_line.find("libUnreal.so") != std::string::npos ||
                map_line.find("libUnrealEngine.so") != std::string::npos ||
                map_line.find("libUE4.so") != std::string::npos)
                has_unreal = true;
            if (map_line.find(package) != std::string::npos) maps_package = true;
        }
        return true;
    };

    // Fast path: only processes whose cmdline names the requested package need
    // their maps parsed. This path runs every 500ms while the overlay is live.
    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        const long candidate = std::strtol(entry->d_name, &end, 10);
        if (!end || *end != '\0' || candidate <= 0) continue;
        ++process_entries;
        const std::string cmdline_path = std::string("/proc/") + entry->d_name + "/cmdline";
        std::ifstream cmdline(cmdline_path, std::ios::binary);
        std::string name;
        std::getline(cmdline, name, '\0');
        const bool exact = name == package;
        const bool child = name.size() > package.size() &&
            name.compare(0, package.size(), package) == 0 && name[package.size()] == ':';
        if (!exact && !child) continue;
        ++cmdline_candidates;
        bool has_unreal = false;
        bool maps_package = false;
        inspect_maps(entry->d_name, has_unreal, maps_package);
        const bool mapped_target = has_unreal && maps_package;
        if (mapped_target) ++mapped_package_candidates;
        const pid_t candidate_pid = static_cast<pid_t>(candidate);
        const int score = (mapped_target ? 1000 : 0) + (has_unreal ? 100 : 0) + (exact ? 20 : 0) +
                          (candidate_pid == pid_.load(std::memory_order_relaxed) ? 1 : 0);
        if (score > best_score) {
            best_score = score;
            found = candidate_pid;
        }
    }
    closedir(proc);

    // Fallback only when cmdline is genuinely hidden. Parsing every process
    // maps on each retry caused the render thread to stall on busy devices.
    if (found <= 0 && cmdline_candidates == 0) {
        proc = opendir("/proc");
        if (!proc) {
            set_error(std::string("opendir /proc fallback: ") + std::strerror(errno));
            return false;
        }
        while (dirent* entry = readdir(proc)) {
            char* end = nullptr;
            const long candidate = std::strtol(entry->d_name, &end, 10);
            if (!end || *end != '\0' || candidate <= 0) continue;
            bool has_unreal = false;
            bool maps_package = false;
            if (!inspect_maps(entry->d_name, has_unreal, maps_package) || !has_unreal || !maps_package)
                continue;
            ++mapped_package_candidates;
            const pid_t candidate_pid = static_cast<pid_t>(candidate);
            const int score = 1000 + (candidate_pid == pid_.load(std::memory_order_relaxed) ? 1 : 0);
            if (score > best_score) {
                best_score = score;
                found = candidate_pid;
            }
        }
        closedir(proc);
    }
    if (found <= 0) {
        std::ostringstream message;
        message << "target process not found package=" << package
                << " proc=" << process_entries
                << " cmdline=" << cmdline_candidates
                << " mapped=" << mapped_package_candidates
                << " maps_open_fail=" << maps_open_failures;
        set_error(message.str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pid_.load(std::memory_order_relaxed) != found) {
            detach_unlocked();
            pid_.store(found, std::memory_order_release);
        }
    }
    return true;
}

bool ProcessMemory::refresh_module(std::string_view module) {
    if (!alive()) { set_error("target process is not alive"); return false; }
    const std::string maps_path = "/proc/" + std::to_string(pid_.load(std::memory_order_acquire)) + "/maps";
    std::ifstream maps(maps_path);
    if (!maps) { set_error("cannot open " + maps_path); return false; }
    std::string line;
    const std::string requested(module);
    std::uintptr_t fallback = 0;
    while (std::getline(maps, line)) {
        std::istringstream stream(line);
        std::string range, perms, offset_text, path;
        if (!(stream >> range >> perms >> offset_text)) continue;
        std::getline(stream, path);
        const auto first_path = path.find_first_not_of(" \t");
        if (first_path == std::string::npos) continue;
        path = path.substr(first_path);
        const auto deleted = path.find(" (deleted)");
        if (deleted != std::string::npos) path.resize(deleted);
        const auto slash = path.find_last_of('/');
        const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
        if (basename != requested && path != requested) continue;
        const auto dash = range.find('-');
        if (dash == std::string::npos) continue;
        const auto start = static_cast<std::uintptr_t>(std::strtoull(range.substr(0, dash).c_str(), nullptr, 16));
        const auto file_offset = std::strtoull(offset_text.c_str(), nullptr, 16);
        if (!fallback) fallback = start;
        if (file_offset == 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            module_base_.store(start, std::memory_order_release);
            module_name_ = basename;
            return true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        module_base_.store(fallback, std::memory_order_release);
        if (!fallback) { last_error_ = "module not found: " + requested; return false; }
        module_name_ = requested;
    }
    return true;
}

bool ProcessMemory::refresh_module_candidates(const std::vector<std::string>& modules) {
    const std::string previous_error = last_error();
    for (const auto& module: modules) {
        if (module.empty()) continue;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            module_base_.store(0, std::memory_order_release);
            module_name_.clear();
        }
        if (refresh_module(module)) return true;
    }
    if (!modules.empty()) set_error("module not found: " + modules.front());
    else set_error(previous_error.empty() ? "module candidates are empty" : previous_error);
    return false;
}

bool ProcessMemory::valid_pointer(std::uintptr_t address) const {
    return address >= 0x10000ULL && address <= 0x0000FFFFFFFFFFFFULL && (address % alignof(void*) == 0);
}

bool ProcessMemory::valid_address_range(std::uintptr_t address, std::size_t size) const {
    constexpr std::uintptr_t maximum = 0x0000FFFFFFFFFFFFULL;
    if (size == 0 || address < 0x10000ULL || address > maximum) return false;
    return static_cast<std::uintptr_t>(size - 1U) <= maximum - address;
}

std::optional<MemoryRange> ProcessMemory::mapping_for(std::uintptr_t address) const {
    if (!alive() || !valid_pointer(address)) return std::nullopt;
    std::ifstream maps("/proc/" + std::to_string(pid_.load(std::memory_order_acquire)) + "/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream stream(line);
        std::string range, permissions, offset, device, inode, path;
        if (!(stream >> range >> permissions >> offset >> device >> inode) ||
            permissions.empty() || permissions[0] != 'r') continue;
        std::getline(stream, path);
        const auto path_start = path.find_first_not_of(" \t");
        path = path_start == std::string::npos ? std::string{} : path.substr(path_start);
        const auto dash = range.find('-');
        if (dash == std::string::npos) continue;
        const auto start = static_cast<std::uintptr_t>(std::strtoull(range.substr(0, dash).c_str(), nullptr, 16));
        const auto end = static_cast<std::uintptr_t>(std::strtoull(range.substr(dash + 1).c_str(), nullptr, 16));
        if (address >= start && address < end)
            return MemoryRange{start, end, permissions.size() > 1 && permissions[1] == 'w',
                               permissions.size() > 2 && permissions[2] == 'x',
                               permissions.size() > 3 && permissions[3] == 'p', path};
    }
    return std::nullopt;
}

std::vector<MemoryRange> ProcessMemory::readable_mappings() const {
    return readable_mappings(pid_.load(std::memory_order_acquire));
}

std::vector<MemoryRange> ProcessMemory::readable_mappings(pid_t target_pid) const {
    std::vector<MemoryRange> result;
    if (target_pid <= 0 || (kill(target_pid, 0) != 0 && errno != EPERM)) return result;
    std::ifstream maps("/proc/" + std::to_string(target_pid) + "/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream stream(line);
        std::string range, permissions, offset, device, inode, path;
        if (!(stream >> range >> permissions >> offset >> device >> inode) ||
            permissions.empty() || permissions[0] != 'r') continue;
        std::getline(stream, path);
        const auto path_start = path.find_first_not_of(" \t");
        path = path_start == std::string::npos ? std::string{} : path.substr(path_start);
        const auto dash = range.find('-');
        if (dash == std::string::npos) continue;
        const auto start = static_cast<std::uintptr_t>(std::strtoull(range.substr(0, dash).c_str(), nullptr, 16));
        const auto end = static_cast<std::uintptr_t>(std::strtoull(range.substr(dash + 1).c_str(), nullptr, 16));
        if (end <= start) continue;
        result.push_back({start, end, permissions.size() > 1 && permissions[1] == 'w',
                          permissions.size() > 2 && permissions[2] == 'x',
                          permissions.size() > 3 && permissions[3] == 'p', path});
    }
    return result;
}

bool ProcessMemory::open_mem_fallback() const {
    if (mem_fd_ >= 0) return true;
    const pid_t pid = pid_.load(std::memory_order_acquire);
    const std::string path = "/proc/" + std::to_string(pid) + "/mem";
    mem_fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (mem_fd_ < 0) mem_fd_ = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    return mem_fd_ >= 0;
}

bool ProcessMemory::open_mem_writable() const {
    if (mem_fd_ >= 0) {
        const int flags = fcntl(mem_fd_, F_GETFL);
        if (flags >= 0 && (flags & O_ACCMODE) == O_RDWR) return true;
        close(mem_fd_);
        mem_fd_ = -1;
    }
    const pid_t pid = pid_.load(std::memory_order_acquire);
    const std::string path = "/proc/" + std::to_string(pid) + "/mem";
    mem_fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
    return mem_fd_ >= 0;
}

bool ProcessMemory::read_bytes(std::uintptr_t address, void* out, std::size_t size) const {
    if (!out || !valid_address_range(address, size) || !alive()) return false;
    iovec local{out, size};
    iovec remote{reinterpret_cast<void*>(address), size};
    const pid_t pid = pid_.load(std::memory_order_acquire);
    const auto copied = syscall(__NR_process_vm_readv, pid, &local, 1UL, &remote, 1UL, 0UL);
    if (copied == static_cast<ssize_t>(size)) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_mem_fallback() && pread64(mem_fd_, out, size, static_cast<off64_t>(address)) == static_cast<ssize_t>(size)) return true;
    last_error_ = std::string("read 0x") + std::to_string(address) + ": " + std::strerror(errno);
    return false;
}

bool ProcessMemory::read_scatter(const std::vector<MemoryRead>& reads) const {
    if (reads.empty()) return true;
    if (!alive()) return false;
    constexpr std::size_t kMaximumIovecs = 128;
    std::vector<iovec> local;
    std::vector<iovec> remote;
    local.reserve(kMaximumIovecs);
    remote.reserve(kMaximumIovecs);
    bool all_ok = true;
    for (std::size_t begin = 0; begin < reads.size(); begin += kMaximumIovecs) {
        const std::size_t end = std::min(reads.size(), begin + kMaximumIovecs);
        std::size_t cursor = begin;
        while (cursor < end) {
            local.clear();
            remote.clear();
            std::size_t expected{};
            std::size_t built{};
            for (std::size_t index = cursor; index < end; ++index) {
                const auto& read = reads[index];
                if (!read.output || !valid_address_range(read.address, read.size)) break;
                local.push_back({read.output, read.size});
                remote.push_back({reinterpret_cast<void*>(read.address), read.size});
                expected += read.size;
                ++built;
            }
            if (local.empty()) {
                const auto& failed = reads[cursor++];
                if (failed.output && failed.size) std::memset(failed.output, 0, failed.size);
                all_ok = false;
                continue;
            }
            const pid_t pid = pid_.load(std::memory_order_acquire);
            const auto copied = syscall(__NR_process_vm_readv, pid, local.data(), local.size(),
                                        remote.data(), remote.size(), 0UL);
            if (copied == static_cast<ssize_t>(expected)) {
                cursor += built;
                if (cursor < end) {
                    const auto& invalid = reads[cursor++];
                    if (invalid.output && invalid.size) std::memset(invalid.output, 0, invalid.size);
                    all_ok = false;
                }
                continue;
            }

            // process_vm_readv stops at the first inaccessible remote iovec.
            // Retain its completed prefix, zero only the failed element, and
            // continue with the suffix instead of replaying all 128 reads.
            all_ok = false;
            std::size_t completed{};
            std::size_t bytes{};
            if (copied > 0) {
                while (completed < local.size() &&
                       bytes + local[completed].iov_len <= static_cast<std::size_t>(copied)) {
                    bytes += local[completed].iov_len;
                    ++completed;
                }
            }
            cursor += completed;
            if (cursor < end) {
                const auto& failed = reads[cursor++];
                if (failed.output && failed.size) std::memset(failed.output, 0, failed.size);
            }
        }
    }
    return all_ok;
}

bool ProcessMemory::write_bytes(std::uintptr_t address, const void* data, std::size_t size) const {
    if (!data || !valid_address_range(address, size) || !alive()) return false;
    iovec local{const_cast<void*>(data), size};
    iovec remote{reinterpret_cast<void*>(address), size};
    const pid_t pid = pid_.load(std::memory_order_acquire);
    const auto copied = syscall(__NR_process_vm_writev, pid, &local, 1UL, &remote, 1UL, 0UL);
    if (copied == static_cast<ssize_t>(size)) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_mem_writable() &&
        pwrite64(mem_fd_, data, size, static_cast<off64_t>(address)) == static_cast<ssize_t>(size))
        return true;
    last_error_ = std::string("write 0x") + std::to_string(address) + ": " + std::strerror(errno);
    return false;
}

}  // namespace rebootnova
