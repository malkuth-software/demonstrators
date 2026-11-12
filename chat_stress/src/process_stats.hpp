#pragma once

// Process figures for the workload records (CHAT_STRESS_SPEC section 6):
// footprint, CPU time, process start, heap in use and a description of the
// machine. Each is host-specific code (macOS, Windows, Linux); GUI.Forms
// offers no portable diagnostics call for them (GAPS.md 9). A figure a host
// cannot give is reported absent, never guessed.

#include <cstdint>
#include <optional>
#include <string>

namespace chat_stress {

// macOS task_vm_info.phys_footprint, Windows PrivateWorkingSetSize, Linux
// Pss from /proc/self/smaps_rollup.
[[nodiscard]] std::optional<std::uint64_t> process_footprint_bytes();

// User plus system CPU time of the process, microseconds.
[[nodiscard]] std::optional<std::uint64_t> process_cpu_microseconds();

// The process's start, microseconds since the Unix epoch (macOS
// proc_pidinfo, Windows GetProcessTimes, Linux /proc/self/stat field 22).
[[nodiscard]] std::optional<std::int64_t> process_start_unix_microseconds();

// Now, microseconds since the Unix epoch (system clock).
[[nodiscard]] std::int64_t unix_microseconds_now();

// The C heap's blocks and bytes in use, read without hooking the allocator
// (macOS malloc_zone_statistics over every zone, glibc mallinfo2). A
// difference between two readings is a net figure (allocations less frees),
// not an allocation count: GUI.Forms has no public allocation counter
// (GAPS.md 6) and the app installs no allocator of its own.
struct HeapReading final {
    std::uint64_t blocks_in_use{};
    std::uint64_t bytes_in_use{};
};
[[nodiscard]] std::optional<HeapReading> heap_in_use();

// The machine, read in-process (no shell): model, processor, memory,
// logical processors, operating system and version.
struct MachineDescription final {
    std::string host_kind{};       // "macos-arm64", "windows-x64", "linux-x64", ...
    std::string model{};           // hw.model on macOS
    std::string processor{};
    std::uint64_t memory_bytes{};
    std::uint32_t logical_processors{};
    std::string os_name{};
    std::string os_version{};
    std::string os_build{};
};
[[nodiscard]] MachineDescription describe_machine();

} // namespace chat_stress
