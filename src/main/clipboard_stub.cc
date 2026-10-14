#include <string_view>

#include "clipboard.h"

namespace island {

// No clipboard helper on this platform yet: the copy shortcut reports failure
// and leaves the clipboard alone.
bool CopyTextToClipboard(std::string_view) { return false; }

}  // namespace island
