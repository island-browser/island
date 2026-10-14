#ifndef ISLAND_CLIPBOARD_H_
#define ISLAND_CLIPBOARD_H_

#include <string_view>

namespace island {

// Puts plain UTF-8 text on the system clipboard. Returns false when the
// platform has no clipboard support here or the write fails. The macOS
// implementation lives in macos/clipboard_mac.mm; Windows and Linux use
// clipboard_stub.cc until a native clipboard helper exists for them.
[[nodiscard]] bool CopyTextToClipboard(std::string_view text);

}  // namespace island

#endif  // ISLAND_CLIPBOARD_H_
