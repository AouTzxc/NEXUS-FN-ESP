#include "rebootnova/runtime_log.h"

#include <android/log.h>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>

namespace rebootnova::runtime {
namespace {

int g_log_fd = -1;
const char* volatile g_stage = "pre-main";

void write_line(const char* text, bool sync = false) {
    if (!text) return;
    const std::size_t length = std::strlen(text);
    if (g_log_fd >= 0) {
        (void)::write(g_log_fd, text, length);
        (void)::write(g_log_fd, "\n", 1);
        if (sync) (void)::fsync(g_log_fd);
    }
    __android_log_write(ANDROID_LOG_INFO, "RebootNova", text);
}

void crash_handler(int signal_number, siginfo_t* info, void* context) {
    std::uintptr_t pc = 0;
#if defined(__aarch64__)
    if (context) pc = reinterpret_cast<ucontext_t*>(context)->uc_mcontext.pc;
#endif
    Dl_info dl{};
    const bool resolved = pc && dladdr(reinterpret_cast<void*>(pc), &dl) != 0;
    const auto base = resolved ? reinterpret_cast<std::uintptr_t>(dl.dli_fbase) : 0;
    const auto relative = base && pc >= base ? pc - base : 0;
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "CRASH signal=%d stage=%s fault=%p pc=0x%llx base=0x%llx relative_pc=0x%llx image=%s",
                  signal_number, g_stage ? g_stage : "unknown", info ? info->si_addr : nullptr,
                  static_cast<unsigned long long>(pc), static_cast<unsigned long long>(base),
                  static_cast<unsigned long long>(relative), resolved && dl.dli_fname ? dl.dli_fname : "unknown");
    write_line(message, true);
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal_number, &action, nullptr);
    raise(signal_number);
}

}  // namespace

bool initialize(const char* path) {
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
    if (path && path[0]) {
        g_log_fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
        if (g_log_fd < 0) return false;
    }
    write_line("RebootNova runtime log");
    return true;
}

void install_crash_handlers() {
    struct sigaction action{};
    action.sa_sigaction = crash_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    constexpr int crash_signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
    for (const int signal_number : crash_signals)
        sigaction(signal_number, &action, nullptr);
}

void set_stage(const char* stage) {
    g_stage = stage;
}

void log(const char* format, ...) {
    char message[768]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    write_line(message);
}

void shutdown() {
    write_line("runtime shutdown", true);
    if (g_log_fd >= 0) close(g_log_fd);
    g_log_fd = -1;
}

}  // namespace rebootnova::runtime
