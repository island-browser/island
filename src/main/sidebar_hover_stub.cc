#include "sidebar_state.h"

// Windows and Linux get no native hover seam in this feature: the spec defers
// them explicitly and Cmd/Ctrl+B covers those platforms through the same
// SidebarState machine. The seam's C++-visible contract is fixed here so a
// later implementation drops in without touching chrome or window code.
namespace island {

void* InstallSidebarHoverSeam(void*, SidebarHoverCallback, void*) { return nullptr; }

void RemoveSidebarHoverSeam(void*) {}

}  // namespace island
