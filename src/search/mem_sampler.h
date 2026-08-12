#ifndef ISLAND_SEARCH_MEM_SAMPLER_H_
#define ISLAND_SEARCH_MEM_SAMPLER_H_

#include <cstdint>

namespace island {
namespace search {

// Process resident memory sampling.
//
// Returns the current resident-set size of this process in bytes:
//   - macOS:    task_info(TASK_VM_INFO) phys_footprint
//   - Linux:    /proc/self/statm resident pages * page size
//   - Windows:  GetProcessMemoryInfo WorkingSetSize
//   - elsewhere: 0
class MemSampler {
  public:
    MemSampler() = delete;

    static std::uint64_t ResidentBytes();
};

}  // namespace search
}  // namespace island

#endif