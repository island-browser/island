#include "link_open_policy.h"

namespace island {

LinkOpenAction DecideLinkOpen(LinkDisposition disposition, bool user_gesture) noexcept {
    if (!user_gesture) {
        return LinkOpenAction::kReject;
    }
    switch (disposition) {
        case LinkDisposition::kNewBackgroundTab:
            return LinkOpenAction::kPeek;
        case LinkDisposition::kNewWindow:
        case LinkDisposition::kNewForegroundTab:
            return LinkOpenAction::kForegroundTab;
        case LinkDisposition::kCurrentTab:
        case LinkDisposition::kOther:
            return LinkOpenAction::kReject;
    }
    return LinkOpenAction::kReject;
}

}  // namespace island
