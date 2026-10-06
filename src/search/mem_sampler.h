#ifndef ISLAND_SEARCH_MEM_SAMPLER_H_
#define ISLAND_SEARCH_MEM_SAMPLER_H_

#include <cstdint>

namespace island {
namespace search {

// Process resident memory sampling.
//
// Returns the current resident-set size of this process in bytes:
//   - macOS:    task_info(MACH_TASK_BASIC_INFO) resident_size
//   - Linux:    /proc/self/statm resident pages * page size
//   - Windows:  GetProcessMemoryInfo WorkingSetSize
//   - elsewhere: 0
// Every implementation counts resident file-backed pages, so a mapped
// segment's touched pages show up. 0 means the sample failed (or there is no
// sampler for this platform); callers must treat it as an error, never as a
// measurement.
class MemSampler {
  public:
    MemSampler() = delete;

    static std::uint64_t ResidentBytes();
};

}  // namespace search
}  // namespace island

#endif