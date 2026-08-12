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
#include <psapi.h>
#include <windows.h>
#endif

namespace island {
namespace search {

std::uint64_t MemSampler::ResidentBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t kr =
        task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (kr != KERN_SUCCESS) {
        return 0;
    }
    // phys_footprint is the amount of memory actually charged to this process:
    // resident private pages plus compressed-page accounting, excluding file-
    // backed pages the kernel can evict. It is the fairest RSS measure for a
    // memory-gate ceiling on macOS.
    return static_cast<std::uint64_t>(info.phys_footprint);
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
