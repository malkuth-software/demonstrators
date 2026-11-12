#include "process_stats.hpp"

#include <chrono>
#include <fstream>
#include <thread>

#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace chat_stress {

namespace {

#if defined(__APPLE__)
// A sysctl string, empty when the name is unknown.
std::string sysctl_string(const char* name) {
    std::size_t size = 0U;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0U) != 0 || size == 0U) return std::string{};
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0U) != 0) return std::string{};
    while (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

std::uint64_t sysctl_u64(const char* name) {
    std::uint64_t value = 0U;
    std::size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0U) != 0 || size != sizeof(value)) return 0U;
    return value;
}
#endif

#if defined(__linux__)
// The value after "key" and a colon on the first matching line of a file.
std::string file_field(const char* path, const std::string& key) {
    std::ifstream file(path);
    std::string line{};
    while (std::getline(file, line)) {
        if (line.compare(0U, key.size(), key) != 0) continue;
        std::size_t start = line.find_first_of(":=", key.size());
        if (start == std::string::npos) continue;
        ++start;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || line[start] == '"')) ++start;
        std::string value = line.substr(start);
        while (!value.empty() && (value.back() == '"' || value.back() == ' ')) value.pop_back();
        return value;
    }
    return std::string{};
}
#endif

} // namespace

std::int64_t unix_microseconds_now() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::microseconds since = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return static_cast<std::int64_t>(since.count());
}

std::optional<std::uint64_t> process_footprint_bytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (result != KERN_SUCCESS) return std::nullopt;
    return static_cast<std::uint64_t>(info.phys_footprint);
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX2 counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) == 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(counters.PrivateWorkingSetSize);
#elif defined(__linux__)
    const std::string pss = file_field("/proc/self/smaps_rollup", "Pss");
    if (pss.empty()) return std::nullopt;
    const std::uint64_t kilobytes = std::stoull(pss);
    return kilobytes * 1024U;
#else
    return std::nullopt;
#endif
}

std::optional<std::uint64_t> process_cpu_microseconds() {
#if defined(_WIN32)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) return std::nullopt;
    const std::uint64_t kernel_100ns = (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32U) | kernel.dwLowDateTime;
    const std::uint64_t user_100ns = (static_cast<std::uint64_t>(user.dwHighDateTime) << 32U) | user.dwLowDateTime;
    return (kernel_100ns + user_100ns) / 10U;
#elif defined(__APPLE__) || defined(__linux__)
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return std::nullopt;
    const std::uint64_t user = static_cast<std::uint64_t>(usage.ru_utime.tv_sec) * 1'000'000U +
                               static_cast<std::uint64_t>(usage.ru_utime.tv_usec);
    const std::uint64_t system = static_cast<std::uint64_t>(usage.ru_stime.tv_sec) * 1'000'000U +
                                 static_cast<std::uint64_t>(usage.ru_stime.tv_usec);
    return user + system;
#else
    return std::nullopt;
#endif
}

std::optional<std::int64_t> process_start_unix_microseconds() {
#if defined(__APPLE__)
    struct proc_bsdinfo info {};
    const int size = proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, static_cast<int>(sizeof(info)));
    if (size != static_cast<int>(sizeof(info))) return std::nullopt;
    const std::int64_t start = static_cast<std::int64_t>(info.pbi_start_tvsec) * 1'000'000 +
                               static_cast<std::int64_t>(info.pbi_start_tvusec);
    return start;
#elif defined(_WIN32)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) return std::nullopt;
    const std::uint64_t since_1601 = (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32U) | creation.dwLowDateTime;
    constexpr std::uint64_t epoch_difference_100ns = 116'444'736'000'000'000ULL;
    return static_cast<std::int64_t>((since_1601 - epoch_difference_100ns) / 10U);
#elif defined(__linux__)
    // Field 22 is the start in clock ticks after boot; boot time from /proc/stat.
    std::ifstream stat("/proc/self/stat");
    std::string content{};
    std::getline(stat, content);
    const std::size_t close = content.rfind(')');
    if (close == std::string::npos) return std::nullopt;
    std::size_t position = close + 2U;
    // After ')' come fields 3, 4, ...: skip to field 22 (19 more spaces).
    for (int field = 3; field < 22 && position < content.size(); ++field) {
        position = content.find(' ', position);
        if (position == std::string::npos) return std::nullopt;
        ++position;
    }
    const std::uint64_t ticks = std::stoull(content.substr(position));
    const std::string boot = file_field("/proc/stat", "btime");
    if (boot.empty()) return std::nullopt;
    const std::int64_t boot_seconds = static_cast<std::int64_t>(std::stoll(boot));
    const long ticks_per_second = sysconf(_SC_CLK_TCK);
    if (ticks_per_second <= 0) return std::nullopt;
    const std::int64_t start = boot_seconds * 1'000'000 +
                               static_cast<std::int64_t>(ticks) * 1'000'000 / static_cast<std::int64_t>(ticks_per_second);
    return start;
#else
    return std::nullopt;
#endif
}

MachineDescription describe_machine() {
    MachineDescription machine{};
#if defined(__aarch64__) || defined(_M_ARM64)
    const std::string architecture = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const std::string architecture = "x64";
#else
    const std::string architecture = "other";
#endif
    machine.logical_processors = std::thread::hardware_concurrency();
#if defined(__APPLE__)
    machine.host_kind = "macos-" + architecture;
    machine.model = sysctl_string("hw.model");
    machine.processor = sysctl_string("machdep.cpu.brand_string");
    machine.memory_bytes = sysctl_u64("hw.memsize");
    machine.os_name = "macOS";
    machine.os_version = sysctl_string("kern.osproductversion");
    machine.os_build = sysctl_string("kern.osversion");
#elif defined(_WIN32)
    machine.host_kind = "windows-" + architecture;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) != 0) machine.memory_bytes = memory.ullTotalPhys;
    machine.os_name = "Windows";
    char processor[256]{};
    DWORD size = sizeof(processor);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, processor, &size) == ERROR_SUCCESS) {
        machine.processor = processor;
    }
    char build[64]{};
    size = sizeof(build);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuild",
                     RRF_RT_REG_SZ, nullptr, build, &size) == ERROR_SUCCESS) {
        machine.os_build = build;
    }
    char display[64]{};
    size = sizeof(display);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion",
                     RRF_RT_REG_SZ, nullptr, display, &size) == ERROR_SUCCESS) {
        machine.os_version = display;
    }
#elif defined(__linux__)
    machine.host_kind = "linux-" + architecture;
    machine.processor = file_field("/proc/cpuinfo", "model name");
    const std::string total = file_field("/proc/meminfo", "MemTotal");
    if (!total.empty()) machine.memory_bytes = std::stoull(total) * 1024U;
    machine.os_name = file_field("/etc/os-release", "NAME");
    machine.os_version = file_field("/etc/os-release", "VERSION_ID");
    struct utsname names {};
    if (uname(&names) == 0) machine.os_build = names.release;
    std::ifstream product("/sys/devices/virtual/dmi/id/product_name");
    std::getline(product, machine.model);
#else
    machine.host_kind = "other-" + architecture;
#endif
    return machine;
}

} // namespace chat_stress
