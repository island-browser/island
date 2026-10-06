#include "search/mem_sampler.h"

#include <cstdint>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task_info.h>
#elif defined(__linux__)
#include <unistd.h>

#include <cstdio>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
// <windows.h> must precede <psapi.h>: psapi.h declares its API using windef.h
// types (BOOL, DWORD, HWND) that only windows.h defines, so the reversed
// order fails to compile under MSVC.
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace island {
namespace search {

std::uint64_t MemSampler::ResidentBytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                       reinterpret_cast<task_info_t>(&info), &count);
    if (kr != KERN_SUCCESS) {
        return 0;
    }
    // resident_size is what the S0 design's measurement methodology names, and
    // it counts every resident page -- including the clean file-backed pages
    // of the mmap'd segment, which the budget explicitly charges to search.
    // phys_footprint would leave those out and under-report the gate.
    return static_cast<std::uint64_t>(info.resident_size);
#elif defined(__linux__)
    unsigned long total_pages = 0;
    unsigned long resident_pages = 0;
    std::FILE* const statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr) {
        return 0;
    }
    const int matched = std::fscanf(statm, "%lu %lu", &total_pages, &resident_pages);
    std::fclose(statm);
    if (matched != 2) {
        return 0;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(resident_pages) * static_cast<std::uint64_t>(page_size);
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    const BOOL ok = GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    if (!ok) {
        return 0;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#else
    return 0;
#endif
}

}  // namespace search
}  // namespace island
